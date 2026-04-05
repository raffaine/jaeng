#include "scene.h"

#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include "render/graph/render_graph.h"
#include "common/math/conventions.h"

const auto SceneConvention = jaeng::math::ClipSpaceConvention{ .handed = jaeng::math::Handedness::Left, .depth = jaeng::math::DepthRange::ZeroToOne };

Scene::Scene(const std::string& name, std::unique_ptr<ISpatialPartitioner> partitioner, std::unique_ptr<ICamera> camera, PipelineCache* pc, std::weak_ptr<IMeshSystem> mes, std::weak_ptr<IMaterialSystem> mas, std::weak_ptr<RendererAPI> r) 
    : name(name)
    , partitioner(std::move(partitioner))
    , camera(std::move(camera))
    , pipelineCache(pc)
    , meshSys(mes)
    , matSys(mas)
    , renderer(r)
{}

void Scene::buildDrawList(const jaeng::math::AABB& volume)
{
    // Clears the list (if any system is not available, no point in keeping it)
    drawList.clear();

    // Retrieve the reference to Mesh and Material Systems
    auto meshSystem = meshSys.lock();
    auto matSystem = matSys.lock();
    if (!meshSystem || !matSystem) return;


    // Collect the ComponentPack of all entities in the volume and iterate
    auto proxies = partitioner->queryVisible(volume);
    for (auto& proxy : proxies) {
        auto* mesh  = meshSystem->getMesh(proxy.mesh).orValue(nullptr);
        auto* matBg = matSystem->getBindData(proxy.material).orValue(nullptr);

        // Collet the Material Bindings and Mesh Data, if not available, skip
        if(!mesh || !matBg) continue;

        // Creates or Retrieves the Pipeline
        PipelineCache::Key pk { .material = proxy.material, .topology = mesh->topology };
        auto pso = pipelineCache->getPipeline(pk);
        if (!pso.has_value()) {
            auto gfx = renderer.lock();
            if (!gfx) return; // no point in continuing without the renderer
            // Creates Pipeline and Stores it in the cache
            GraphicsPipelineDesc pdesc{matBg->vertexShader, matBg->pixelShader, mesh->topology, matBg->vertexLayout, TextureFormat::BGRA8_UNORM};
            pdesc.depth_stencil.enableDepth = true; // always enable depth for now (TODO: material should tell this)
            pso = gfx->create_graphics_pipeline(&pdesc);
            pipelineCache->storePipeline(pk, *pso);
        }

        // Create the transform matrix for entity
        auto worldMat = glm::identity<glm::mat4>();
        worldMat = glm::translate(worldMat, proxy.transform.position);
        worldMat *= glm::toMat4(proxy.transform.rotation);
        worldMat = glm::scale(worldMat, proxy.transform.scale);

        // TODO: Sort data in a way that shared pipeline and group bindings are grouped together in the same DrawBatch
        // for now just create Batches with one packet on it
        DrawPacket dp{.worldMatrix  = std::move(worldMat),
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
}

struct CBMaterial {
    glm::vec4 baseColor;
    float roughness;
    float metallic;
};

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
                    auto viewProj = camera->getViewProj(SceneConvention);
                    ctx.gfx->update_buffer(db.cbFrame, 0, &viewProj, sizeof(glm::mat4));
                    // Bind Frame CB to slot 0 (register b1)
                    ctx.gfx->cmd_bind_uniform(ctx.cmd, 0, db.cbFrame, 0);
                }

                auto* matBg = matSysRef->getBindData(db.material).orValue(nullptr);

                for (auto& dp : db.packets) {
                    ctx.gfx->cmd_set_vertex_buffer(ctx.cmd, 0, dp.vertexBuffer, 0);
                    ctx.gfx->cmd_set_index_buffer(ctx.cmd, dp.indexBuffer, true, 0);

                    // Push Bindless Indices (Texture and Sampler)
                    if (matBg) {
                        uint32_t indices[2] = { 0, 0 };
                        if (!matBg->textureIndices.empty()) indices[0] = matBg->textureIndices[0];
                        if (!matBg->samplerIndices.empty()) indices[1] = matBg->samplerIndices[0];
                        ctx.gfx->cmd_push_constants(ctx.cmd, 0, 2, indices);
                    }
                    
                    // Update and bind Object CB to slot 1 (register b2)
                    ctx.gfx->update_buffer(db.constant, 0, &dp.worldMatrix, sizeof(glm::mat4));
                    ctx.gfx->cmd_bind_uniform(ctx.cmd, 1, db.constant, 0);

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
