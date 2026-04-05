#include "vulkan_resources.h"
#include "vulkan_device.h"
#include "vulkan_descriptors.h"
#include "vulkan_context.h"
#include <vector>
#include <cstring>

namespace jaeng::renderer {

jaeng::result<VulkanBuffer> create_vulkan_buffer(VulkanDevice* device, const BufferDesc* desc, const void* initial_data) {
    vk::BufferUsageFlags usage;
    if (desc->usage & BufferUsage_Vertex) usage |= vk::BufferUsageFlagBits::eVertexBuffer;
    if (desc->usage & BufferUsage_Index) usage |= vk::BufferUsageFlagBits::eIndexBuffer;
    if (desc->usage & BufferUsage_Uniform) usage |= vk::BufferUsageFlagBits::eUniformBuffer;
    if (desc->usage & BufferUsage_Upload) usage |= vk::BufferUsageFlagBits::eTransferSrc;

    vk::BufferCreateInfo bufferInfo({}, desc->size_bytes, usage);
    vk::Buffer buffer = device->device.createBuffer(bufferInfo);

    vk::MemoryRequirements memReqs = device->device.getBufferMemoryRequirements(buffer);
    vk::MemoryPropertyFlags memProps = vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent;
    
    vk::MemoryAllocateInfo allocInfo(memReqs.size, device->findMemoryType(memReqs.memoryTypeBits, memProps));
    vk::DeviceMemory memory = device->device.allocateMemory(allocInfo);

    device->device.bindBufferMemory(buffer, memory, 0);

    if (initial_data) {
        void* data = device->device.mapMemory(memory, 0, desc->size_bytes);
        memcpy(data, initial_data, desc->size_bytes);
        device->device.unmapMemory(memory);
    }

    return VulkanBuffer{ buffer, memory, desc->size_bytes };
}

jaeng::result<VulkanTexture> create_vulkan_texture(VulkanDevice* device, VulkanDescriptorHeap* heap, const TextureDesc* desc, const void* initial_data) {
    vk::Format format = vk::Format::eR8G8B8A8Unorm; 
    vk::ImageCreateInfo imageInfo(
        {}, vk::ImageType::e2D, format, vk::Extent3D{ desc->width, desc->height, 1 },
        1, 1, vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal,
        vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst
    );

    vk::Image image = device->device.createImage(imageInfo);
    vk::MemoryRequirements memReqs = device->device.getImageMemoryRequirements(image);
    vk::MemoryAllocateInfo allocInfo(memReqs.size, device->findMemoryType(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal));
    vk::DeviceMemory memory = device->device.allocateMemory(allocInfo);
    device->device.bindImageMemory(image, memory, 0);

    if (initial_data) {
        uint64_t size = desc->width * desc->height * 4;
        BufferDesc stagingDesc{ size, BufferUsage_Upload };
        auto stagingRes = create_vulkan_buffer(device, &stagingDesc, initial_data);
        if (stagingRes.hasValue()) {
            auto staging = std::move(stagingRes).logError().value();
            vk::CommandBufferAllocateInfo cbAlloc(g_ctx->commandPool, vk::CommandBufferLevel::ePrimary, 1);
            vk::CommandBuffer cb = device->device.allocateCommandBuffers(cbAlloc)[0];
            cb.begin({ vk::CommandBufferUsageFlagBits::eOneTimeSubmit });

            vk::ImageMemoryBarrier barrier(
                {}, vk::AccessFlagBits::eTransferWrite,
                vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                image, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
            );
            cb.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer, {}, nullptr, nullptr, barrier);

            vk::BufferImageCopy region(0, 0, 0, { vk::ImageAspectFlagBits::eColor, 0, 0, 1 }, { 0, 0, 0 }, { desc->width, desc->height, 1 });
            cb.copyBufferToImage(staging.buffer, image, vk::ImageLayout::eTransferDstOptimal, region);

            barrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
            barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
            barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            cb.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader, {}, nullptr, nullptr, barrier);

            cb.end();
            vk::SubmitInfo submit({}, {}, cb);
            device->graphicsQueue.submit(submit, nullptr);
            device->graphicsQueue.waitIdle();
            device->device.freeCommandBuffers(g_ctx->commandPool, cb);
            device->device.destroyBuffer(staging.buffer);
            device->device.freeMemory(staging.memory);
        }
    }

    vk::ImageViewCreateInfo viewInfo(
        {}, image, vk::ImageViewType::e2D, format, {},
        { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
    );
    vk::ImageView view = device->device.createImageView(viewInfo);

    uint32_t srvIndex = heap->allocateSrv();
    heap->updateSrv(srvIndex, view, vk::ImageLayout::eShaderReadOnlyOptimal);

    return VulkanTexture{ image, memory, view, srvIndex };
}

} // namespace jaeng::renderer
