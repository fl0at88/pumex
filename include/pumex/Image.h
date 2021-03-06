//
// Copyright(c) 2017-2018 Paweł Księżopolski ( pumexx )
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
#include <vulkan/vulkan.h>
#if defined(GLM_ENABLE_EXPERIMENTAL) // hack around redundant GLM_ENABLE_EXPERIMENTAL defined in type.hpp
  #undef GLM_ENABLE_EXPERIMENTAL
  #define GLM_ENABLE_EXPERIMENTAL_HACK
#endif
#include <gli/texture.hpp>
#if defined(GLM_ENABLE_EXPERIMENTAL_HACK)
  #define GLM_ENABLE_EXPERIMENTAL
  #undef GLM_ENABLE_EXPERIMENTAL_HACK
#endif
#include <pumex/Export.h>
#include <pumex/ResourceRange.h>
#include <pumex/DeviceMemoryAllocator.h>

namespace pumex
{

PUMEX_EXPORT VkExtent3D            makeVkExtent3D(const ImageSize& iSize);
PUMEX_EXPORT VkExtent3D            makeVkExtent3D(const ImageSize& iSize, const VkExtent3D& extent);
PUMEX_EXPORT VkExtent3D            makeVkExtent3D(const ImageSize& iSize, const VkExtent2D& extent);
PUMEX_EXPORT VkExtent2D            makeVkExtent2D(const ImageSize& iSize);
PUMEX_EXPORT VkExtent2D            makeVkExtent2D(const ImageSize& iSize, const VkExtent2D& extent);
PUMEX_EXPORT VkRect2D              makeVkRect2D(int32_t x, int32_t y, uint32_t width, uint32_t height);
PUMEX_EXPORT VkRect2D              makeVkRect2D(const ImageSize& iSize);
PUMEX_EXPORT VkRect2D              makeVkRect2D(const ImageSize& iSize, const VkExtent2D& extent);
PUMEX_EXPORT VkViewport            makeVkViewport(float x, float y, float width, float height, float minDepth, float maxDepth);
PUMEX_EXPORT VkSampleCountFlagBits makeSamples(uint32_t samples);
PUMEX_EXPORT VkSampleCountFlagBits makeSamples(const ImageSize& iSize);

// struct representing all options required to create or describe VkImage
struct PUMEX_EXPORT ImageTraits
{
  explicit ImageTraits()                            = default;
  explicit ImageTraits(VkFormat format, ImageSize imageSize, VkImageUsageFlags usage,
    bool linearTiling = false, VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED, VkImageCreateFlags imageCreate = 0,
    VkImageType imageType = VK_IMAGE_TYPE_2D, VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE);
  ImageTraits(const ImageTraits& traits)            = default;
  ImageTraits& operator=(const ImageTraits& traits) = default;

  VkFormat                 format         = VK_FORMAT_R8G8B8A8_UNORM;
  ImageSize                imageSize      = ImageSize{ isAbsolute, glm::vec3{1.0f, 1.0f, 1.0f}, 1, 1 };
  VkImageUsageFlags        usage          = VK_IMAGE_USAGE_SAMPLED_BIT;
  bool                     linearTiling   = false;
  VkImageLayout            initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImageCreateFlags       imageCreate    = 0;
  VkImageType              imageType      = VK_IMAGE_TYPE_2D;
  VkSharingMode            sharingMode    = VK_SHARING_MODE_EXCLUSIVE;
};

// Class implementing Vulkan image (VkImage ) on a single device/surface
class PUMEX_EXPORT Image
{
public:
  Image()                            = delete;
  // user creates VkImage and assigns memory to it
  explicit Image(Device* device, const ImageTraits& imageTraits, std::shared_ptr<DeviceMemoryAllocator> allocator);
  // user delivers VkImage, Image does not own it, just creates VkImageView
  explicit Image(Device* device, VkImage image, VkFormat format, const ImageSize& imageSize = ImageSize{ isAbsolute, glm::vec3{ 1.0f, 1.0f, 1.0f }, 1, 1 });
  Image(const Image&)                = delete;
  Image& operator=(const Image&)     = delete;
  Image(Image&&)                     = delete;
  Image& operator=(Image&&)          = delete;
  virtual ~Image();

  inline VkDevice           getDevice() const;
  inline VkImage            getHandleImage() const;
  inline VkDeviceSize       getMemorySize() const;
  inline const ImageTraits& getImageTraits() const;

  void                      getImageSubresourceLayout(VkImageSubresource& subRes, VkSubresourceLayout& subResLayout) const;
  void*                     mapMemory(size_t offset, size_t range, VkMemoryMapFlags flags=0);
  void                      unmapMemory();
protected:
  ImageTraits                            imageTraits;
  VkDevice                               device       = VK_NULL_HANDLE;
  std::shared_ptr<DeviceMemoryAllocator> allocator;
  VkImage                                image        = VK_NULL_HANDLE;
  DeviceMemoryBlock                      memoryBlock;
  bool                                   ownsImage    = true;
};

// inlines
VkDevice             Image::getDevice() const      { return device; }
VkImage              Image::getHandleImage() const { return image; }
VkDeviceSize         Image::getMemorySize() const  { return memoryBlock.alignedSize; }
const ImageTraits&   Image::getImageTraits() const { return imageTraits; }

// helper functions
PUMEX_EXPORT ImageTraits        getImageTraitsFromTexture(const gli::texture& texture, VkImageUsageFlags usage);

PUMEX_EXPORT VkFormat           vulkanFormatFromGliFormat(gli::texture::format_type format);
PUMEX_EXPORT VkImageViewType    vulkanViewTypeFromGliTarget(gli::texture::target_type target);
PUMEX_EXPORT VkImageType        vulkanImageTypeFromTextureExtents(const gli::extent3d& extents);
PUMEX_EXPORT VkComponentSwizzle vulkanSwizzlesFromGliSwizzles(const gli::swizzle& s);
PUMEX_EXPORT VkComponentMapping vulkanComponentMappingFromGliComponentMapping(const gli::swizzles& swz);

// Texture files are loaded through TextureLoader. Currently only gli library is used to load them
// This is temporary solution.
class PUMEX_EXPORT TextureLoader
{
public:
  TextureLoader()                                = delete;
  explicit TextureLoader(const std::vector<std::string>& supportedExtensions);
  TextureLoader(const TextureLoader&)            = delete;
  TextureLoader& operator=(const TextureLoader&) = delete;
  TextureLoader(TextureLoader&&)                 = delete;
  TextureLoader& operator=(TextureLoader&&)      = delete;

  inline const std::vector<std::string>& getSupportedExtensions() const;
  virtual std::shared_ptr<gli::texture>  load(const std::string& fileName, bool buildMipMaps) = 0;
protected:
  std::vector<std::string> supportedExtensions;
};

const std::vector<std::string>& TextureLoader::getSupportedExtensions() const { return supportedExtensions; }


}
