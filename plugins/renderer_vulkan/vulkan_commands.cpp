#include "vulkan_context.h"
#include <vector>
#include <array>
#include <cstring>

using namespace jaeng::renderer;

extern "C" {

CommandListHandle vk_begin_commands() {
    if (!g_ctx) return 0;
    g_ctx->commandBuffer.reset();
    g_ctx->commandBuffer.begin({vk::CommandBufferUsageFlagBits::eOneTimeSubmit});

    // Automatically sync updated buffers
    for (auto& [h, b] : g_ctx->buffers) {
        if (b.needsBarrier) {
            vk::BufferMemoryBarrier barrier(
                vk::AccessFlagBits::eHostWrite,
                vk::AccessFlagBits::eUniformRead | vk::AccessFlagBits::eVertexAttributeRead | vk::AccessFlagBits::eIndexRead,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                b.buffer, 0, VK_WHOLE_SIZE
            );
            g_ctx->commandBuffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eHost,
                vk::PipelineStageFlagBits::eAllGraphics,
                {},
                nullptr,
                barrier,
                nullptr
            );
            b.needsBarrier = false;
        }
    }

    return (CommandListHandle)1;
}

void vk_cmd_begin_pass(CommandListHandle, LoadOp load_op, const ColorAttachmentDesc* colors, uint32_t count, const DepthAttachmentDesc* depth) {
    if (!g_ctx) return;
    std::vector<vk::RenderingAttachmentInfo> colorAttachments;
    vk::Extent2D extent(1280, 720);

    bool hasSwapchain = false;
    if (!g_ctx->swapchains.empty()) {
        auto it = g_ctx->swapchains.begin();
        if (it->second.swapchain) {
            extent = it->second.extent;
            hasSwapchain = true;
        }
    }

    for (uint32_t i = 0; i < count; ++i) {
        vk::ImageView view;
        vk::Image image;

        if ((colors[i].tex & 0xFFFF0000) == 0xFFFF0000) {
            if (!hasSwapchain) {
                JAENG_LOG_ERROR("vk_cmd_begin_pass: color attachment {} is swapchain but no swapchain found", i);
                continue;
            }
            auto& s = g_ctx->swapchains.begin()->second; 
            uint32_t idx = s.currentImageIndex; 
            view = s.imageViews[idx];
            image = s.images[idx];
        } else {
            auto it = g_ctx->textures.find(colors[i].tex);
            if (it == g_ctx->textures.end()) {
                JAENG_LOG_ERROR("vk_cmd_begin_pass: color attachment {} texture handle {} not found", i, colors[i].tex);
                continue;
            }
            view = it->second.view;
            image = it->second.image;
        }
        
        // Use a barrier to transition the image to ColorAttachmentOptimal
        // If we are clearing, we transition from Undefined
        vk::ImageMemoryBarrier barrier(
            {}, vk::AccessFlagBits::eColorAttachmentWrite,
            (load_op == LoadOp::Clear) ? vk::ImageLayout::eUndefined : vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eColorAttachmentOptimal,
            VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
            image, { vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1 }
        );
        g_ctx->commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eColorAttachmentOutput, {}, nullptr, nullptr, barrier);

        vk::ClearValue clearColor(vk::ClearColorValue(std::array<float, 4>{colors[i].clear_rgba[0], colors[i].clear_rgba[1], colors[i].clear_rgba[2], colors[i].clear_rgba[3]}));
        
        vk::RenderingAttachmentInfo info(view, vk::ImageLayout::eColorAttachmentOptimal);
        info.loadOp = (load_op == LoadOp::Clear) ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
        info.storeOp = vk::AttachmentStoreOp::eStore;
        info.clearValue = clearColor;
        colorAttachments.push_back(info);
    }

    vk::RenderingAttachmentInfo depthAttachment;
    vk::RenderingAttachmentInfo* pDepthAttachment = nullptr;

    if (depth && depth->tex != 0) {
        vk::Image depthImage;
        vk::ImageView depthView;

        if ((depth->tex & 0xFFFF0000) == 0xFFFE0000) {
            if (hasSwapchain) {
                auto& s = g_ctx->swapchains.begin()->second;
                depthImage = s.depthImage;
                depthView = s.depthView;
            } else {
                depthImage = nullptr;
            }
        } else {
            auto it = g_ctx->textures.find(depth->tex);
            if (it != g_ctx->textures.end()) {
                depthImage = it->second.image;
                depthView = it->second.view;
            } else {
                depthImage = nullptr;
            }
        }

        if (depthImage) {
            vk::ImageMemoryBarrier barrier(
                {}, vk::AccessFlagBits::eDepthStencilAttachmentWrite,
                (load_op == LoadOp::Clear) ? vk::ImageLayout::eUndefined : vk::ImageLayout::eDepthStencilAttachmentOptimal,
                vk::ImageLayout::eDepthStencilAttachmentOptimal,
                VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED,
                depthImage, { vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1 }
            );
            g_ctx->commandBuffer.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eEarlyFragmentTests, {}, nullptr, nullptr, barrier);

            depthAttachment = vk::RenderingAttachmentInfo(depthView, vk::ImageLayout::eDepthStencilAttachmentOptimal);
            depthAttachment.loadOp = (load_op == LoadOp::Clear) ? vk::AttachmentLoadOp::eClear : vk::AttachmentLoadOp::eLoad;
            depthAttachment.storeOp = vk::AttachmentStoreOp::eStore;
            depthAttachment.clearValue = vk::ClearValue(vk::ClearDepthStencilValue(depth->clear_d, 0));
            pDepthAttachment = &depthAttachment;
        }
    }

    vk::Viewport viewport(0.0f, (float)extent.height, (float)extent.width, -(float)extent.height, 0.0f, 1.0f);
    vk::Rect2D scissor({0, 0}, extent);
    g_ctx->commandBuffer.setViewport(0, viewport);
    g_ctx->commandBuffer.setScissor(0, scissor);

    vk::RenderingInfo renderingInfo({}, {{0, 0}, extent}, 1, 0, colorAttachments, pDepthAttachment);
    g_ctx->commandBuffer.beginRendering(renderingInfo);
}

void vk_cmd_end_pass(CommandListHandle) {
    if (!g_ctx) return;
    g_ctx->commandBuffer.endRendering();
}

void vk_cmd_draw(CommandListHandle, uint32_t vtx_count, uint32_t instance_count, uint32_t first_vtx, uint32_t first_instance) {
    if (!g_ctx) return;
    g_ctx->commandBuffer.draw(vtx_count, instance_count, first_vtx, first_instance);
}

void vk_cmd_set_pipeline(CommandListHandle, PipelineHandle h) {
    if (!g_ctx) return;
    auto it = g_ctx->pipelines.find(h);
    if (it == g_ctx->pipelines.end()) {
        JAENG_LOG_ERROR("vk_cmd_set_pipeline: pipeline handle {} not found", h);
        return;
    }
    auto& p = it->second;
    g_ctx->commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, p.pipeline);
    g_ctx->currentPipelineLayout = p.layout;
    
    std::vector<vk::DescriptorSet> sets = {
        g_ctx->descriptors.getSet(0),
        g_ctx->descriptors.getSet(1),
        g_ctx->descriptors.getSet(2)
    };
    g_ctx->commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, p.layout, 0, sets, nullptr);
}

void vk_cmd_push_constants(CommandListHandle, uint32_t offset, uint32_t count, const void* d) {
    if (!g_ctx) return;
    if (g_ctx->currentPipelineLayout) {
        g_ctx->commandBuffer.pushConstants(g_ctx->currentPipelineLayout, vk::ShaderStageFlagBits::eAll, offset * 4, count * 4, d);
    }
}

void vk_cmd_set_vertex_buffer(CommandListHandle, uint32_t slot, BufferHandle h, uint64_t offset) {
    if (!g_ctx) return;
    auto it = g_ctx->buffers.find(h);
    if (it == g_ctx->buffers.end()) {
        JAENG_LOG_ERROR("vk_cmd_set_vertex_buffer: buffer handle {} not found", h);
        return;
    }
    g_ctx->commandBuffer.bindVertexBuffers(slot, it->second.buffer, offset);
}

void vk_cmd_set_index_buffer(CommandListHandle, BufferHandle h, bool index32, uint64_t offset) {
    if (!g_ctx) return;
    auto it = g_ctx->buffers.find(h);
    if (it == g_ctx->buffers.end()) {
        JAENG_LOG_ERROR("vk_cmd_set_index_buffer: buffer handle {} not found", h);
        return;
    }
    g_ctx->commandBuffer.bindIndexBuffer(it->second.buffer, offset, index32 ? vk::IndexType::eUint32 : vk::IndexType::eUint16);
}

void vk_cmd_bind_uniform(CommandListHandle, uint32_t slot, BufferHandle h, uint64_t offset) {
    if (!g_ctx) return;
    auto it = g_ctx->buffers.find(h);
    if (it == g_ctx->buffers.end()) {
        JAENG_LOG_ERROR("vk_cmd_bind_uniform: buffer handle {} not found", h);
        return;
    }
    auto& b = it->second;

    // slot maps to HLSL register bN, which maps to binding N in Set 0
    g_ctx->descriptors.updateUniform(slot + 1, b.buffer, offset, b.size);
}

void vk_cmd_barrier(CommandListHandle, BufferHandle h, uint32_t src_access, uint32_t dst_access) {
    if (!g_ctx) return;
    
    vk::AccessFlags srcFlags;
    vk::AccessFlags dstFlags;
    vk::PipelineStageFlags srcStage = vk::PipelineStageFlagBits::eAllCommands;
    vk::PipelineStageFlags dstStage = vk::PipelineStageFlagBits::eAllCommands;

    auto mapAccess = [](uint32_t flags, vk::AccessFlags& vkFlags, vk::PipelineStageFlags& vkStage) {
        if (flags & Access_HostWrite) {
            vkFlags |= vk::AccessFlagBits::eHostWrite;
            vkStage |= vk::PipelineStageFlagBits::eHost;
        }
        if (flags & Access_UniformRead) {
            vkFlags |= vk::AccessFlagBits::eUniformRead;
            vkStage |= vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader;
        }
        if (flags & Access_VertexRead) {
            vkFlags |= vk::AccessFlagBits::eVertexAttributeRead;
            vkStage |= vk::PipelineStageFlagBits::eVertexInput;
        }
        if (flags & Access_IndexRead) {
            vkFlags |= vk::AccessFlagBits::eIndexRead;
            vkStage |= vk::PipelineStageFlagBits::eVertexInput;
        }
        if (flags & Access_ShaderRead) {
            vkFlags |= vk::AccessFlagBits::eShaderRead;
            vkStage |= vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader;
        }
        if (flags & Access_ShaderWrite) {
            vkFlags |= vk::AccessFlagBits::eShaderWrite;
            vkStage |= vk::PipelineStageFlagBits::eVertexShader | vk::PipelineStageFlagBits::eFragmentShader;
        }
        if (flags & Access_ColorWrite) {
            vkFlags |= vk::AccessFlagBits::eColorAttachmentWrite;
            vkStage |= vk::PipelineStageFlagBits::eColorAttachmentOutput;
        }
        if (flags & Access_DepthWrite) {
            vkFlags |= vk::AccessFlagBits::eDepthStencilAttachmentWrite;
            vkStage |= vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests;
        }
        if (vkStage == vk::PipelineStageFlags()) vkStage = vk::PipelineStageFlagBits::eTopOfPipe;
    };

    mapAccess(src_access, srcFlags, srcStage);
    mapAccess(dst_access, dstFlags, dstStage);

    if (h == 0) {
        vk::MemoryBarrier barrier(srcFlags, dstFlags);
        g_ctx->commandBuffer.pipelineBarrier(srcStage, dstStage, {}, barrier, nullptr, nullptr);
    } else {
        auto it = g_ctx->buffers.find(h);
        if (it != g_ctx->buffers.end()) {
            vk::BufferMemoryBarrier barrier(srcFlags, dstFlags, VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, it->second.buffer, 0, VK_WHOLE_SIZE);
            g_ctx->commandBuffer.pipelineBarrier(srcStage, dstStage, {}, nullptr, barrier, nullptr);
        }
    }
}

void vk_cmd_draw_indexed(CommandListHandle, uint32_t idx_count, uint32_t inst_count, uint32_t first_idx, int32_t vtx_offset, uint32_t first_instance) {
    if (!g_ctx) return;
    g_ctx->commandBuffer.drawIndexed(idx_count, inst_count, first_idx, vtx_offset, first_instance);
}

void vk_end_commands(CommandListHandle) {
    if (!g_ctx) return;
    g_ctx->commandBuffer.end();
}

void vk_submit(CommandListHandle* lists, uint32_t list_count) {
    if (!g_ctx) return;
    
    bool hasActiveSwapchain = false;
    for (auto& [h, s] : g_ctx->swapchains) {
        if (s.swapchain) {
            hasActiveSwapchain = true;
            break;
        }
    }
    if (!hasActiveSwapchain) return;

    vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eColorAttachmentOutput};
    vk::SubmitInfo submitInfo(1, &g_ctx->imageAvailableSemaphore, waitStages, 1, &g_ctx->commandBuffer, 1, &g_ctx->renderFinishedSemaphore);
    (void)g_ctx->device.graphicsQueue.submit(1, &submitInfo, g_ctx->inFlightFence);
}

} // extern "C"
