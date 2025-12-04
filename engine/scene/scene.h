#pragma once

#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>

#include "ipartition.h"
#include "pipelinecache.h"
#include "render/public/renderer_api.h"
#include "material/imaterialsys.h"
#include "mesh/imeshsys.h"

// Forward declarations
class RenderGraph;

// Represents a logical rendering context
class Scene {
public:
    Scene(const std::string& name, std::unique_ptr<ISpatialPartitioner> partitioner, PipelineCache* pc, std::weak_ptr<IMeshSystem>, std::weak_ptr<IMaterialSystem>, std::weak_ptr<RendererAPI>);

    const std::string& getName() const { return name; }
    
    // Builts batched draw commands for Render Graph
    void buildDrawList(const jaeng::math::AABB&);

    // Creates the needed passes on Render Graph
    void renderScene(RenderGraph& rg, SwapchainHandle swap);

    // Access partitioner for queries
    ISpatialPartitioner* getPartitioner() const { return partitioner.get(); }

private:
    std::string name;
    
    // Partitioner decides how to cluster entities 
    std::unique_ptr<ISpatialPartitioner> partitioner;

    // Per-Instance Resources for Drawing
    struct DrawPacket {
        int entityId;
        glm::mat4 worldMatrix;
        BufferHandle vertexBuffer;
        BufferHandle indexBuffer;
        uint32_t indexCount;
        BindGroupHandle instanceBindGroup; // Per-object resources (optional if instancing)
    };

    // Shared Instance Resources for Drawing
    struct DrawBatch {
        PipelineHandle pipeline;
        MaterialHandle material;
        BufferHandle   constant;
        BindGroupHandle materialBindGroup; // Shared resources
        std::vector<DrawPacket> packets;
    };

    // Draw List created for Frame (follows latest build command)
    std::vector<DrawBatch> drawList;
    PipelineCache* pipelineCache;
    std::weak_ptr<IMeshSystem> meshSys;
    std::weak_ptr<IMaterialSystem> matSys;
    std::weak_ptr<RendererAPI> renderer;
};

// Orchestrates multiple scenes
class SceneManager {
public:
    SceneManager(std::shared_ptr<IMeshSystem>, std::shared_ptr<IMaterialSystem>, std::shared_ptr<RendererAPI>);

    jaeng::result<Scene*> createScene(const std::string& name, std::unique_ptr<ISpatialPartitioner> partitioner);

    void destroyScene(const std::string& name);

    Scene* getScene(const std::string& name);

private:
    std::unordered_map<std::string, std::unique_ptr<Scene>> scenes;
    std::weak_ptr<IMeshSystem> meshSys;
    std::weak_ptr<IMaterialSystem> matSys;
    std::weak_ptr<RendererAPI> renderer;
    std::unique_ptr<PipelineCache> pipelineCache;
};
