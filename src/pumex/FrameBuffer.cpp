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

#include <pumex/FrameBuffer.h>
#include <pumex/RenderPass.h>
#include <pumex/Surface.h>
#include <pumex/Command.h>
#include <pumex/RenderContext.h>
#include <pumex/utils/Log.h>

namespace pumex
{

FrameBufferImageDefinition::FrameBufferImageDefinition(AttachmentType at, VkFormat f, VkImageUsageFlags u, VkImageAspectFlags am, VkSampleCountFlagBits s, const std::string& n, const AttachmentSize& as, const gli::swizzles& sw)
  : attachmentType{ at }, format{ f }, usage{ u }, aspectMask{ am }, samples{ s }, name{ n }, attachmentSize { as }, swizzles{ sw }
{
}

FrameBufferImages::FrameBufferImages(const std::vector<FrameBufferImageDefinition>& fbid, std::shared_ptr<DeviceMemoryAllocator> a)
  : imageDefinitions(fbid), allocator{ a }
{
}

FrameBufferImages::~FrameBufferImages()
{
  for (auto it : perSurfaceData)
    it.second.frameBufferImages.clear();
}

void FrameBufferImages::invalidate(Surface* surface)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto it = perSurfaceData.find(surface->surface);
  if(it != perSurfaceData.end())
    it->second.valid = false;
}

void FrameBufferImages::validate(Surface* surface)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(surface->surface);
  if (pddit == perSurfaceData.end())
    pddit = perSurfaceData.insert({ surface->surface, PerSurfaceData(surface->device.lock()->device,imageDefinitions.size()) }).first;
  if (pddit->second.valid)
    return;

  for (uint32_t i = 0; i < pddit->second.frameBufferImages.size(); ++i)
    pddit->second.frameBufferImages[i] = nullptr;

  for (uint32_t i = 0; i < imageDefinitions.size(); i++)
  {
    FrameBufferImageDefinition& definition = imageDefinitions[i];
    if (definition.attachmentType == atSurface)
      continue;
    VkExtent3D imSize;
    switch (definition.attachmentSize.attachmentSize)
    {
    case AttachmentSize::SurfaceDependent:
    {
      imSize.width  = surface->swapChainSize.width  * definition.attachmentSize.imageSize.x;
      imSize.height = surface->swapChainSize.height * definition.attachmentSize.imageSize.y;
      imSize.depth  = 1;
      break;
    }
    case AttachmentSize::Absolute:
    {
      imSize.width  = definition.attachmentSize.imageSize.x;
      imSize.height = definition.attachmentSize.imageSize.y;
      imSize.depth  = 1;
      break;
    }
    }
    ImageTraits imageTraits(definition.usage, definition.format, imSize, false, 1, 1,
      definition.samples, VK_IMAGE_LAYOUT_UNDEFINED, definition.aspectMask, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0, VK_IMAGE_TYPE_2D, VK_SHARING_MODE_EXCLUSIVE,
      VK_IMAGE_VIEW_TYPE_2D, definition.swizzles);
    pddit->second.frameBufferImages[i] = std::make_shared<Image>(surface->device.lock().get(), imageTraits, allocator);
  }
  pddit->second.valid = true;
}

void FrameBufferImages::reset(Surface* surface)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(surface->surface);
  if (pddit != perSurfaceData.end())
  {
    pddit->second.frameBufferImages.clear();
    perSurfaceData.erase(surface->surface);
  }
}

Image* FrameBufferImages::getImage(Surface* surface, uint32_t imageIndex)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(surface->surface);
  if (pddit == perSurfaceData.end())
    return nullptr;
  if (pddit->second.frameBufferImages.size() <= imageIndex)
    return nullptr;
  return pddit->second.frameBufferImages[imageIndex].get();
}

FrameBufferImageDefinition FrameBufferImages::getSwapChainDefinition()
{
  for (const auto& d : imageDefinitions)
    if (d.attachmentType == atSurface)
      return d;
  return FrameBufferImageDefinition(atSurface, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, VK_IMAGE_ASPECT_COLOR_BIT, VK_SAMPLE_COUNT_1_BIT, "swapchain");
}

FrameBuffer::FrameBuffer(std::shared_ptr<Surface> s, uint32_t count)
  : surface{ s }
{
  valid.resize(count, false);
  frameBuffers.resize(count, VK_NULL_HANDLE);
}

FrameBuffer::~FrameBuffer()
{
  reset();
}

void FrameBuffer::setFrameBufferImages(std::shared_ptr<FrameBufferImages> fbi)
{
  frameBufferImages = fbi;
  invalidate();
}

void FrameBuffer::setRenderPass(std::shared_ptr<RenderPass> rp)
{
  renderPass = rp;
  invalidate();
}

void FrameBuffer::reset()
{
  auto device = surface.lock()->device.lock();
  std::lock_guard<std::mutex> lock(mutex);
  for (auto f : frameBuffers)
    vkDestroyFramebuffer(device->device, f, nullptr);
}

void FrameBuffer::invalidate()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (uint32_t i = 0; i<valid.size(); ++i)
    valid[i] = false;
}


void FrameBuffer::validate(uint32_t index, const std::vector<std::unique_ptr<Image>>& swapChainImages)
{
  std::lock_guard<std::mutex> lock(mutex);

  uint32_t activeIndex = index % swapChainImages.size();
  if (valid[activeIndex])
    return;

  CHECK_LOG_THROW(renderPass.use_count() == 0, "FrameBuffer::validate() : render pass was not defined");
  std::shared_ptr<RenderPass> rp = renderPass.lock();
  std::shared_ptr<Surface> surf  = surface.lock();
  std::shared_ptr<Device> device = surf->device.lock();

  if (frameBuffers[activeIndex] != VK_NULL_HANDLE)
    vkDestroyFramebuffer(device->device, frameBuffers[activeIndex], nullptr);

  // create frame buffer images ( render pass attachments ), skip images marked as swap chain images ( as they're created already )
  std::vector<VkImageView> imageViews;
  imageViews.resize(rp->attachments.size());
  for (uint32_t i = 0; i < rp->attachments.size(); i++)
  {
    AttachmentDefinition& definition          = rp->attachments[i];
    FrameBufferImageDefinition& fbiDefinition = frameBufferImages->imageDefinitions[definition.imageDefinitionIndex];
    if (fbiDefinition.attachmentType == atSurface)
    {
      imageViews[i] = swapChainImages[activeIndex]->getImageView();
      continue;
    }
    else
    {
      imageViews[i] = frameBufferImages->getImage(surface.lock().get(), definition.imageDefinitionIndex)->getImageView();
    }
  }

  // define frame buffers
  VkFramebufferCreateInfo frameBufferCreateInfo{};
    frameBufferCreateInfo.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    frameBufferCreateInfo.renderPass      = rp->getHandle(device->device);
    frameBufferCreateInfo.attachmentCount = imageViews.size();
    frameBufferCreateInfo.pAttachments    = imageViews.data();
    frameBufferCreateInfo.width           = surf->swapChainSize.width;
    frameBufferCreateInfo.height          = surf->swapChainSize.height;
    frameBufferCreateInfo.layers          = 1;
  VK_CHECK_LOG_THROW(vkCreateFramebuffer(device->device, &frameBufferCreateInfo, nullptr, &frameBuffers[activeIndex]), "Could not create frame buffer " << activeIndex);
  valid[activeIndex] = true;
  notifyCommandBuffers();
}

VkFramebuffer FrameBuffer::getFrameBuffer(uint32_t index)
{
  std::lock_guard<std::mutex> lock(mutex);
  if (frameBuffers.size() <= index)
    return VK_NULL_HANDLE;
  return frameBuffers[index];
}

InputAttachment::InputAttachment(const std::string& an)
  : Resource{ Resource::OnceForAllSwapChainImages }, attachmentName{ an }
{
}

std::pair<bool, VkDescriptorType> InputAttachment::getDefaultDescriptorType()
{
  return{ true, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT };
}

void InputAttachment::validate(const RenderContext& renderContext)
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(renderContext.vkSurface);
  if (pddit == perSurfaceData.end())
    pddit = perSurfaceData.insert({ renderContext.vkSurface, PerSurfaceData() }).first;
  if (pddit->second.valid)
    return;
  pddit->second.valid = true;
}

void InputAttachment::invalidate()
{
  std::lock_guard<std::mutex> lock(mutex);
  for (auto& pdd : perSurfaceData)
    pdd.second.valid = false;
  invalidateDescriptors();
}

void InputAttachment::getDescriptorSetValues(const RenderContext& renderContext, std::vector<DescriptorSetValue>& values) const
{
  std::lock_guard<std::mutex> lock(mutex);
  auto pddit = perSurfaceData.find(renderContext.vkSurface);
  if (pddit == perSurfaceData.end())
    return;
  auto frameBuffer = renderContext.surface->frameBuffer;
  uint32_t frameBufferIndex = UINT32_MAX;
  for (uint32_t i = 0; i < frameBuffer->getFrameBufferImages()->imageDefinitions.size(); ++i)
  {
    if (frameBuffer->getFrameBufferImages()->imageDefinitions[i].name == attachmentName)
    {
      frameBufferIndex = i;
      break;
    }
  }
  CHECK_LOG_THROW(frameBufferIndex == UINT32_MAX, "Can't find input attachment with name : " << attachmentName);
  values.push_back(DescriptorSetValue(VK_NULL_HANDLE, frameBuffer->getFrameBufferImages()->getImage(renderContext.surface, frameBufferIndex)->getImageView(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
}

}