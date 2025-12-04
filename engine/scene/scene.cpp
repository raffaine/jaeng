#include "scene.h"

#include <glm/gtc/matrix_transform.hpp>

#include "render/graph/render_graph.h"

Scene::Scene(const std::string& name, std::unique_ptr<ISpatialPartitioner> partitioner, PipelineCache* pc, std::weak_ptr<IMeshSystem> mes, std::weak_ptr<IMaterialSystem> mas, std::weak_ptr<RendererAPI> r) 
    : name(name)
    , partitioner(std::move(partitioner))
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
            pso = gfx->create_graphics_pipeline(&pdesc);
            pipelineCache->storePipeline(pk, *pso);
        }

        // TODO: Sort data in a way that shared pipeline and group bindings are grouped together in the same DrawBatch
        // for now just create Batches with one packet on it
        DrawPacket dp{.worldMatrix = (e.transform) ? glm::translate(glm::identity<glm::mat4>(), e.transform->position) : glm::identity<glm::mat4>(),
                      .vertexBuffer = mesh->vertexBuffer,
                      .indexBuffer  = mesh->indexBuffer,
                      .indexCount   = static_cast<uint32_t>(mesh->indexCount)};
        DrawBatch db { .pipeline = *pso, .material = *e.material, .constant = matBg->constantBuffers[0], .materialBindGroup = matBg->bindGroup };
        db.packets.push_back(std::move(dp));

        // Store the Batch on the DrawList
        drawList.push_back(std::move(db));
    }
}

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

                for (auto& dp : db.packets) {
                    ctx.gfx->cmd_set_vertex_buffer(ctx.cmd, 0, dp.vertexBuffer, 0);
                    ctx.gfx->cmd_set_index_buffer(ctx.cmd, dp.indexBuffer, true, 0);

                    ctx.gfx->update_buffer(db.constant, 0, &dp.worldMatrix, sizeof(glm::mat4));
                    ctx.gfx->cmd_set_bind_group(ctx.cmd, 0, db.materialBindGroup);
                    ctx.gfx->cmd_draw_indexed(ctx.cmd, dp.indexCount, 1, 0, 0, 0);
                }
            }
        }
    );
}

SceneManager::SceneManager(std::shared_ptr<IMeshSystem> mes, std::shared_ptr<IMaterialSystem> mas, std::shared_ptr<RendererAPI> r)
    : meshSys(mes), matSys(mas), renderer(r), pipelineCache(std::make_unique<PipelineCache>()) {}
   
jaeng::result<Scene*> SceneManager::createScene(const std::string& name, std::unique_ptr<ISpatialPartitioner> partitioner) {
    auto scene = std::make_unique<Scene>(name, std::move(partitioner), pipelineCache.get(), meshSys, matSys, renderer);
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
