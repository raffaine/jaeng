#include "scene.h"

#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include "render/graph/render_graph.h"
#include "common/math/conventions.h"
#include <algorithm>

namespace jaeng {

Scene::Scene(const std::string& name, std::unique_ptr<ISpatialPartitioner> partitioner, std::unique_ptr<ICamera> camera, PipelineCache* pc, std::weak_ptr<IMeshSystem> mes, std::weak_ptr<IMaterialSystem> mas, std::weak_ptr<RendererAPI> r) 
    : name(name)
    , partitioner(std::move(partitioner))
    , camera(std::move(camera))
    , pipelineCache(pc)
    , meshSys(mes)
    , matSys(mas)
    , renderer(r)
{}

void Scene::buildDrawList(const math::AABB& volume)
{
    // Clears the list (if any system is not available, no point in keeping it)
    drawList.clear();
    uiDrawList.clear();

    // Retrieve the reference to Mesh and Material Systems
    auto meshSystem = meshSys.lock();
    auto matSystem = matSys.lock();
    if (!meshSystem || !matSystem) {
        static bool loggedOnce = false;
        if (!loggedOnce) {
            JAENG_LOG_ERROR("[Scene] Failed to lock meshSystem ({}) or matSystem ({})", (bool)meshSystem, (bool)matSystem);
            loggedOnce = true;
        }
        return;
    }

    // Collect the ComponentPack of all entities in the volume and iterate
    auto proxies = partitioner->queryVisible(volume);
    for (auto& proxy : proxies) {
        auto meshRes = meshSystem->getMesh(proxy.mesh);
        auto matBgRes = matSystem->getBindData(proxy.material);

        if (!meshRes.hasValue() || !matBgRes.hasValue()) continue;
        
        auto* mesh = std::move(meshRes).logError().value();
        auto* matBg = std::move(matBgRes).logError().value();

        // Safety: Ensure shaders and layout are valid handles
        if (matBg->vertexShader == 0 || matBg->pixelShader == 0 || matBg->vertexLayout == 0) continue;

        // Creates or Retrieves the Pipeline
        PipelineCache::Key pk { .material = proxy.material, .topology = mesh->topology, .enableBlend = false };
        auto pso = pipelineCache->getPipeline(pk);
        if (!pso.has_value()) {
            auto gfx = renderer.lock();
            if (!gfx) return; 
            
            // Creates Pipeline and Stores it in the cache
            GraphicsPipelineDesc pdesc{matBg->vertexShader, matBg->pixelShader, mesh->topology, matBg->vertexLayout, TextureFormat::BGRA8_UNORM};
            
            // Get DepthStencil from Material
            auto metaRes = matSystem->getMetadata(proxy.material);
            if (metaRes.hasValue()) {
                auto meta = std::move(metaRes).logError().value();
                pdesc.depth_stencil.enableDepth = meta->depthStencil.depthTest;
                pdesc.depth_stencil.depthWrite = meta->depthStencil.depthWrite;
                pdesc.depth_stencil.depthFunc = DepthStencilOptions::DepthFunc::LessEqual;
            } else {
                pdesc.depth_stencil.enableDepth = true; // fallback
            }

            pdesc.enable_blend = false;
            auto newPso = gfx->create_graphics_pipeline(&pdesc);
            if (newPso == 0) continue; // Skip if pipeline creation failed
            pso = newPso;
            pipelineCache->storePipeline(pk, *pso);
        }

        if (*pso == 0) continue;

        // TODO: Sort data in a way that shared pipeline and group bindings are grouped together in the same DrawBatch
        DrawPacket dp{.entityId = static_cast<int>(proxy.id),
                      .worldMatrix  = proxy.worldMatrix,
                      .color = proxy.color,
                      .vertexBuffer = mesh->vertexBuffer,
                      .indexBuffer  = mesh->indexBuffer,
                      .indexCount   = static_cast<uint32_t>(mesh->indexCount),
                      };
        // In case shader expects material constant buffer
        if (matBg->constantBuffers.size() >= 3) {
            dp.constant = matBg->constantBuffers[2];
        }

        DrawBatch db { .pipeline = *pso, .material = proxy.material, .constant = matBg->constantBuffers[0] };
        // In case shader expects frame constant buffer
        if (matBg->constantBuffers.size() >= 2) {
            db.cbFrame = matBg->constantBuffers[1];
        }
        db.packets.push_back(std::move(dp));

        // Store the Batch on the DrawList
        drawList.push_back(std::move(db));
    }

    // Process UI Proxies
    std::vector<UIRenderProxy> sortedUIProxies;
    sortedUIProxies.reserve(uiProxies.size());
    for (auto& pair : uiProxies) {
        sortedUIProxies.push_back(pair.second);
    }
    
    std::sort(sortedUIProxies.begin(), sortedUIProxies.end(), [](const UIRenderProxy& a, const UIRenderProxy& b) {
        return a.zIndex < b.zIndex; // Lower z-index draws first
    });

    for (auto& proxy : sortedUIProxies) {
        auto meshRes = meshSystem->getMesh(proxy.mesh);
        auto matBgRes = matSystem->getBindData(proxy.material);

        if(!meshRes.hasValue() || !matBgRes.hasValue()) continue;
        auto* mesh = std::move(meshRes).logError().value();
        auto* matBg = std::move(matBgRes).logError().value();

        // Safety: Ensure shaders and layout are valid handles
        if (matBg->vertexShader == 0 || matBg->pixelShader == 0 || matBg->vertexLayout == 0) continue;

        PipelineCache::Key pk { .material = proxy.material, .topology = mesh->topology, .enableBlend = true };
        auto pso = pipelineCache->getPipeline(pk);
        if (!pso.has_value()) {
            auto gfx = renderer.lock();
            if (!gfx) return; 
            GraphicsPipelineDesc pdesc{matBg->vertexShader, matBg->pixelShader, mesh->topology, matBg->vertexLayout, TextureFormat::BGRA8_UNORM};
            pdesc.depth_stencil.enableDepth = false; // Disable depth test for UI
            pdesc.enable_blend = true;
            auto newPso = gfx->create_graphics_pipeline(&pdesc);
            if (newPso == 0) continue;
            pso = newPso;
            pipelineCache->storePipeline(pk, *pso);
        }

        if (*pso == 0) continue;

        // We assume the quad is centered at origin with size 1x1.
        // Transform the 1x1 quad (which extends from -0.5 to 0.5) to match the RectTransform which spans from 0 to W and 0 to H
        // By translating by (X + W/2, Y + H/2) and scaling by (W, H).
        glm::mat4 worldMat = glm::translate(glm::mat4(1.0f), glm::vec3(proxy.x + proxy.w * 0.5f, proxy.y + proxy.h * 0.5f, 0.0f)) *
                             glm::scale(glm::mat4(1.0f), glm::vec3(proxy.w, proxy.h, 1.0f));

        DrawPacket dp{
            .entityId = static_cast<int>(proxy.id),
            .worldMatrix = worldMat,
            .color = proxy.color,
            .uvRect = proxy.uvRect,
            .vertexBuffer = mesh->vertexBuffer,
            .indexBuffer  = mesh->indexBuffer,
            .indexCount   = static_cast<uint32_t>(mesh->indexCount),
            .constant     = matBg->constantBuffers.size() >= 3 ? matBg->constantBuffers[2] : 0,
            .textureOverride = proxy.textureOverride
        };

        DrawBatch db { .pipeline = *pso, .material = proxy.material, .constant = matBg->constantBuffers[0] };
        if (matBg->constantBuffers.size() >= 2) {
            db.cbFrame = matBg->constantBuffers[1];
        }
        db.packets.push_back(std::move(dp));

        uiDrawList.push_back(std::move(db));
    }
}

void Scene::processCommands(const std::vector<RenderCommand>& queue) {
    for (const auto& cmd : queue) {
        switch (cmd.type) {
            case RenderCommandType::Update:
                addOrUpdateProxy(cmd.proxy);
                break;
            case RenderCommandType::Destroy:
                removeProxy(cmd.id);
                break;
            case RenderCommandType::UpdateCamera:
                setCameraViewProj(cmd.cameraViewProj);
                break;
            case RenderCommandType::UpdateUI:
                addOrUpdateUIProxy(cmd.uiProxy);
                break;
            case RenderCommandType::DestroyUI:
                removeUIProxy(cmd.id);
                break;
            case RenderCommandType::ClearUI:
                clearUIProxies();
                break;
        }
    }
    partitioner->build();
}

void Scene::renderScene(RenderGraph& rg, TextureHandle backbuffer, TextureHandle depthBuffer)
{
    // 1) Clear pass
    rg.add_pass("Clear", { {
        .tex = backbuffer,
        .clear_rgba = { 0.07f, 0.08f, 0.12f, 1.0f }
    } }, { .tex = depthBuffer, .clear_depth = 1.0f }, nullptr);

    // 2) Forward pass
    rg.add_pass("Forward", 
        { { .tex = backbuffer } }, { .tex = depthBuffer },
        [&](const RGPassContext& ctx) {
            auto matSysRef = matSys.lock();
            if (!matSysRef) return;

            for (auto& db : drawList) {
                ctx.gfx->cmd_set_pipeline(ctx.cmd, db.pipeline);

                if (db.cbFrame) {
                    ctx.gfx->update_buffer(db.cbFrame, 0, &cachedViewProj, sizeof(glm::mat4));
                    ctx.gfx->cmd_bind_uniform(ctx.cmd, 0, db.cbFrame, 0);
                }

                auto matBgRes = matSysRef->getBindData(db.material);
                if (!matBgRes.hasValue()) continue;
                auto* matBg = std::move(matBgRes).logError().value();

                for (auto& dp : db.packets) {
                    ctx.gfx->cmd_set_vertex_buffer(ctx.cmd, 0, dp.vertexBuffer, 0);
                    ctx.gfx->cmd_set_index_buffer(ctx.cmd, dp.indexBuffer, true, 0);

                    // Push Bindless Indices (Texture and Sampler)
                    if (matBg) {
                        uint32_t indices[2] = { 0, 0 };
                        if (dp.textureOverride != 0) {
                            indices[0] = ctx.gfx->get_texture_index(dp.textureOverride);
                        } else if (!matBg->textureIndices.empty()) {
                            indices[0] = matBg->textureIndices[0];
                        }
                        if (!matBg->samplerIndices.empty()) indices[1] = matBg->samplerIndices[0];
                        ctx.gfx->cmd_push_constants(ctx.cmd, 0, 2, indices);
                    }
                    
                    // Unified update for WorldMatrix + Color + UVRect
                    struct { glm::mat4 w; glm::vec4 c; glm::vec4 uv; } cbData { dp.worldMatrix, dp.color, dp.uvRect };
                    ctx.gfx->update_buffer(db.constant, 0, &cbData, sizeof(cbData));
                    ctx.gfx->cmd_bind_uniform(ctx.cmd, 1, db.constant, 0);

                    ctx.gfx->cmd_draw_indexed(ctx.cmd, dp.indexCount, 1, 0, 0, 0);
                }
            }
        }
    );

    // 3) UI Pass
    rg.add_pass("UI_Pass", 
        { { .tex = backbuffer } }, { .tex = 0 },
        [&](const RGPassContext& ctx) {
            auto matSysRef = matSys.lock();
            if (!matSysRef) return;

            // Simple ortho matrix. Assuming 1280x720. Ideally we'd get this from the viewport/swapchain size.
            // For the sandbox, hardcoding 1280x720 matches the configuration.
            glm::mat4 orthoProj = glm::ortho(0.0f, 1280.0f, 720.0f, 0.0f, -1.0f, 1.0f);

            for (auto& db : uiDrawList) {
                ctx.gfx->cmd_set_pipeline(ctx.cmd, db.pipeline);

                if (db.cbFrame) {
                    ctx.gfx->update_buffer(db.cbFrame, 0, &orthoProj, sizeof(glm::mat4));
                    ctx.gfx->cmd_bind_uniform(ctx.cmd, 0, db.cbFrame, 0);
                }

                auto matBgRes = matSysRef->getBindData(db.material);
                if (!matBgRes.hasValue()) continue;
                auto* matBg = std::move(matBgRes).logError().value();

                for (auto& dp : db.packets) {
                    ctx.gfx->cmd_set_vertex_buffer(ctx.cmd, 0, dp.vertexBuffer, 0);
                    ctx.gfx->cmd_set_index_buffer(ctx.cmd, dp.indexBuffer, true, 0);

                    if (matBg) {
                        uint32_t indices[2] = { 0, 0 };
                        if (dp.textureOverride != 0) {
                            indices[0] = ctx.gfx->get_texture_index(dp.textureOverride);
                        } else if (!matBg->textureIndices.empty()) {
                            indices[0] = matBg->textureIndices[0];
                        }
                        if (!matBg->samplerIndices.empty()) indices[1] = matBg->samplerIndices[0];
                        ctx.gfx->cmd_push_constants(ctx.cmd, 0, 2, indices);
                    }
                    
                    struct { glm::mat4 w; glm::vec4 c; glm::vec4 uv; } cbData { dp.worldMatrix, dp.color, dp.uvRect };
                    ctx.gfx->update_buffer(db.constant, 0, &cbData, sizeof(cbData));
                    ctx.gfx->cmd_bind_uniform(ctx.cmd, 1, db.constant, 0);

                    // which is currently handled globally per material by the app.

                    ctx.gfx->cmd_draw_indexed(ctx.cmd, dp.indexCount, 1, 0, 0, 0);
                }
            }
        }
    );
}

SceneManager::SceneManager(std::shared_ptr<IMeshSystem> mes, std::shared_ptr<IMaterialSystem> mas, std::shared_ptr<RendererAPI> r)
    : meshSys(mes), matSys(mas), renderer(r), pipelineCache(std::make_unique<PipelineCache>()) {}
   
jaeng::result<Scene*> SceneManager::createScene(const std::string& name, std::unique_ptr<ISpatialPartitioner> partitioner, std::unique_ptr<ICamera> camera) {
    JAENG_ERROR_IF(!partitioner || !camera, jaeng::error_code::invalid_args, "[Scene Manager] A Camera and Partitioner is required for a scene");
    auto scene = std::make_unique<Scene>(name, std::move(partitioner), std::move(camera), pipelineCache.get(), meshSys, matSys, renderer);
    scenes[name] = std::move(scene);
    return scenes[name].get();
}

void SceneManager::destroyScene(const std::string& name) {
    scenes.erase(name);
}

Scene* SceneManager::getScene(const std::string& name) {
    auto it = scenes.find(name);
    return (it != scenes.end()) ? it->second.get() : nullptr;
}

} // namespace jaeng
