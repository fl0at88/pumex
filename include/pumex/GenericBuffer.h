//
// Copyright(c) 2017-2018 Pawe� Ksi�opolski ( pumexx )
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//

#pragma once
#include <memory>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include <pumex/Export.h>
#include <pumex/DeviceMemoryAllocator.h>
#include <pumex/Device.h>
#include <pumex/Command.h>
#include <pumex/Resource.h>
#include <pumex/RenderContext.h>
#include <pumex/utils/Buffer.h>
#include <pumex/utils/Log.h>

// Generic Vulkan buffer

namespace pumex
{

template <typename T>
class GenericBuffer : public Resource, public CommandBufferSource
{
public:
  GenericBuffer()                                = delete;
  explicit GenericBuffer(std::shared_ptr<T> data, std::shared_ptr<DeviceMemoryAllocator> allocator, VkBufferUsageFlagBits usage, Resource::SwapChainImageBehaviour swapChainImageBehaviour = Resource::ForEachSwapChainImage);
  GenericBuffer(const GenericBuffer&)            = delete;
  GenericBuffer& operator=(const GenericBuffer&) = delete;
  virtual ~GenericBuffer();

  void               validate(const RenderContext& renderContext) override;
  void               invalidate() override;
  DescriptorSetValue getDescriptorSetValue(const RenderContext& renderContext) override;

  VkBuffer           getBufferHandle(const RenderContext& renderContext);

private:
  struct PerDeviceData
  {
    PerDeviceData(uint32_t ac)
    {
      resize(ac);
    }
    void resize(uint32_t ac)
    {
      valid.resize(ac, false);
      buffer.resize(ac, VK_NULL_HANDLE);
      memoryBlock.resize(ac, DeviceMemoryBlock());
    }

    std::vector<bool>               valid;
    std::vector<VkBuffer>           buffer;
    std::vector<DeviceMemoryBlock>  memoryBlock;
  };

  std::unordered_map<VkDevice, PerDeviceData> perDeviceData;
  std::shared_ptr<T>                          data;
  std::shared_ptr<DeviceMemoryAllocator>      allocator;
  VkBufferUsageFlagBits                       usage;
};

template <typename T>
GenericBuffer<T>::GenericBuffer(std::shared_ptr<T> d, std::shared_ptr<DeviceMemoryAllocator> a, VkBufferUsageFlagBits u, Resource::SwapChainImageBehaviour scib)
  : Resource{ scib }, data{ d }, allocator{ a }, usage{ u }
{
}

template <typename T>
GenericBuffer<T>::~GenericBuffer()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perDeviceData)
  {
    for (uint32_t i = 0; i < pdd.second.buffer.size(); ++i)
    {
      vkDestroyBuffer(pdd.first, pdd.second.buffer[i], nullptr);
      allocator->deallocate(pdd.first, pdd.second.memoryBlock[i]);
    }
  }
}

template <typename T>
DescriptorSetValue GenericBuffer<T>::getDescriptorSetValue(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(renderContext.vkDevice);
  CHECK_LOG_THROW(pddit == end(perDeviceData), "GenericBuffer<T>::getDescriptorSetValue() : storage buffer was not validated");

  return DescriptorSetValue(pddit->second.buffer[renderContext.activeIndex % activeCount], 0, uglyGetSize(*data) );
}

template <typename T>
void GenericBuffer<T>::invalidate()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perDeviceData)
    std::fill(begin(pdd.second.valid), end(pdd.second.valid), false);
  invalidateDescriptors();
}

template <typename T>
void GenericBuffer<T>::validate(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  if ( swapChainImageBehaviour == Resource::ForEachSwapChainImage && renderContext.imageCount > activeCount )
  {
    activeCount = renderContext.imageCount;
    for (auto& pdd : perDeviceData)
      pdd.second.resize(activeCount);
  }
  auto pddit = perDeviceData.find(renderContext.vkDevice);
  if (pddit == end(perDeviceData))
    pddit = perDeviceData.insert({ renderContext.vkDevice, PerDeviceData(activeCount) }).first;
  uint32_t activeIndex = renderContext.activeIndex % activeCount;
  if (pddit->second.valid[activeIndex])
    return;

  if (pddit->second.buffer[activeIndex] != VK_NULL_HANDLE  && pddit->second.memoryBlock[activeIndex].alignedSize < uglyGetSize(*data))
  {
    vkDestroyBuffer(pddit->first, pddit->second.buffer[activeIndex], nullptr);
    allocator->deallocate(pddit->first, pddit->second.memoryBlock[activeIndex]);
    pddit->second.buffer[activeIndex] = VK_NULL_HANDLE;
    pddit->second.memoryBlock[activeIndex] = DeviceMemoryBlock();
  }

  bool memoryIsLocal = ((allocator->getMemoryPropertyFlags() & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) == VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (pddit->second.buffer[activeIndex] == VK_NULL_HANDLE)
  {
    VkBufferCreateInfo bufferCreateInfo{};
      bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
      bufferCreateInfo.usage = usage | (memoryIsLocal ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : 0);
      bufferCreateInfo.size  = std::max<VkDeviceSize>(1, uglyGetSize(*data));
    VK_CHECK_LOG_THROW(vkCreateBuffer(renderContext.vkDevice, &bufferCreateInfo, nullptr, &pddit->second.buffer[activeIndex]), "Cannot create buffer");
    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(renderContext.vkDevice, pddit->second.buffer[activeIndex], &memReqs);
    pddit->second.memoryBlock[activeIndex] = allocator->allocate(renderContext.device, memReqs);
    CHECK_LOG_THROW(pddit->second.memoryBlock[activeIndex].alignedSize == 0, "Cannot create a buffer " << usage);
    allocator->bindBufferMemory(renderContext.device, pddit->second.buffer[activeIndex], pddit->second.memoryBlock[activeIndex].alignedOffset);

    invalidateCommandBuffers();
  }
  if ( uglyGetSize(*data) > 0)
  {
    if (memoryIsLocal)
    {
      std::shared_ptr<StagingBuffer> stagingBuffer = renderContext.device->acquireStagingBuffer( uglyGetPointer(*data), uglyGetSize(*data));
      auto staggingCommandBuffer = renderContext.device->beginSingleTimeCommands(renderContext.commandPool);
      VkBufferCopy copyRegion{};
      copyRegion.size = uglyGetSize(*data);
      staggingCommandBuffer->cmdCopyBuffer(stagingBuffer->buffer, pddit->second.buffer[activeIndex], copyRegion);
      renderContext.device->endSingleTimeCommands(staggingCommandBuffer, renderContext.queue);
      renderContext.device->releaseStagingBuffer(stagingBuffer);
    }
    else
    {
      allocator->copyToDeviceMemory(renderContext.device, pddit->second.memoryBlock[activeIndex].alignedOffset, uglyGetPointer(*data), uglyGetSize(*data), 0);
    }
  }
  pddit->second.valid[activeIndex] = true;
}

template <typename T>
VkBuffer GenericBuffer<T>::getBufferHandle(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perDeviceData.find(renderContext.vkDevice);
  if (pddit == end(perDeviceData))
    return VK_NULL_HANDLE;
  return pddit->second.buffer[renderContext.activeIndex % activeCount];
}

}
