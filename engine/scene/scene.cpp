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
    auto entities = partitioner->queryVisible(volume);
    for (auto& e : entities) {
        // Collet the Material Bindings and Mesh Data, if not available, skip
        if (!e.material || !e.mesh) continue;
        auto* mesh  = meshSystem->getMesh(*e.mesh).orValue(nullptr);
        auto* matBg = matSystem->getBindData(*e.material).orValue(nullptr);
        if(!mesh || !matBg) continue;

        // Creates or Retrieves the Pipeline
        PipelineCache::Key pk { .material = *e.material, .topology = mesh->topology };
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
        if (e.transform) {
            worldMat = glm::translate(worldMat, e.transform->position);
            worldMat *= glm::toMat4(e.transform->rotation);
            worldMat = glm::scale(worldMat, e.transform->scale);
        }

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

        DrawBatch db { .pipeline = *pso, .material = *e.material, .constant = matBg->constantBuffers[0], .materialBindGroup = matBg->bindGroup };
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

void Scene::renderScene(RenderGraph& rg, SwapchainHandle swap)
{
    // Acquire Graphics to retrieve the current back buffer on swapchain to be the render target
    auto gfx = renderer.lock();
    if (!gfx) return;

    // 1) Clear pass
    rg.add_pass("Clear", { {
        .tex = gfx->get_current_backbuffer(swap),
        .clear_rgba = { 0.07f, 0.08f, 0.12f, 1.0f }
    } }, { .tex = 1, .clear_depth = 1.0f }, nullptr);
    // 2) Forward pass
    rg.add_pass("Forward", 
        { { .tex = gfx->get_current_backbuffer(swap) } }, { .tex = 1 /*enable depth*/ },
        [&](const RGPassContext& ctx) {
            for (auto& db : drawList) {
                ctx.gfx->cmd_set_pipeline(ctx.cmd, db.pipeline);

                if (db.cbFrame) {
                    auto viewProj = camera->getViewProj(SceneConvention);
                    ctx.gfx->update_buffer(db.cbFrame, 0, &viewProj, sizeof(glm::mat4));
                    ctx.gfx->cmd_set_frame_cb(ctx.cmd, db.cbFrame);
                }

                for (auto& dp : db.packets) {
                    ctx.gfx->cmd_set_vertex_buffer(ctx.cmd, 0, dp.vertexBuffer, 0);
                    ctx.gfx->cmd_set_index_buffer(ctx.cmd, dp.indexBuffer, true, 0);

                    if (dp.constant) {
                        CBMaterial matBuf{ {1.f,1.f,1.f,1.f}, 100.f, 1.f };
                        ctx.gfx->update_buffer(dp.constant, 0, &matBuf, sizeof(CBMaterial));
                    }
                    ctx.gfx->update_buffer(db.constant, 0, &dp.worldMatrix, sizeof(glm::mat4));
                    ctx.gfx->cmd_set_object_cb(ctx.cmd, db.constant); // b1
                    ctx.gfx->cmd_set_bind_group(ctx.cmd, 0, db.materialBindGroup); // texture srv + sampler
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
