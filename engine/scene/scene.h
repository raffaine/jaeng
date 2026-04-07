#pragma once

#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <functional>

#include "ipartition.h"
#include "icamera.h"
#include "pipelinecache.h"
#include "render/public/renderer_api.h"
#include "material/imaterialsys.h"
#include "mesh/imeshsys.h"

namespace jaeng {

// Forward declarations
class RenderGraph;

// Represents a logical rendering context
class Scene {
public:
    Scene(const std::string& name, std::unique_ptr<ISpatialPartitioner> partitioner, std::unique_ptr<ICamera> camera, PipelineCache* pc, std::weak_ptr<IMeshSystem>, std::weak_ptr<IMaterialSystem>, std::weak_ptr<RendererAPI>);

    const std::string& getName() const { return name; }

    // Accept state updates from the command queue
    void addOrUpdateProxy(const RenderProxy& proxy) { partitioner->addOrUpdate(proxy); }
    void removeProxy(uint32_t id) { partitioner->remove(id); }

    // Builts batched draw commands for Render Graph
    void buildDrawList(const math::AABB&);

    void setCbFrame(BufferHandle h) { cbFrame = h; }

    // Creates the needed passes on Render Graph
    void renderScene(RenderGraph& rg, TextureHandle backbuffer, TextureHandle depthBuffer = 0);

    // Access partitioner for queries
    ISpatialPartitioner* getPartitioner() const { return partitioner.get(); }

    // Access camera
    ICamera* getCamera() const { return camera.get(); }
    void setCameraViewProj(const glm::mat4& vp) { cachedViewProj = vp; }

private:
    std::string name;
    
    // Partitioner decides how to cluster entities 
    std::unique_ptr<ISpatialPartitioner> partitioner;
    // Camera for the scene
    std::unique_ptr<ICamera> camera;
    glm::mat4 cachedViewProj{1.0f};

    // Per-Instance Resources for Drawing
    struct DrawPacket {
        int entityId;
        glm::mat4 worldMatrix;
        BufferHandle vertexBuffer;
        BufferHandle indexBuffer;
        uint32_t indexCount;
        BufferHandle constant;
    };

    // Shared Instance Resources for Drawing
    struct DrawBatch {
        PipelineHandle pipeline;
        MaterialHandle material;
        BufferHandle   constant; // general uniform
        BufferHandle   cbFrame;
        std::vector<DrawPacket> packets;
    };


    // Draw List created for Frame (follows latest build command)
    std::vector<DrawBatch> drawList;
    BufferHandle cbFrame = 0;
    PipelineCache* pipelineCache;
    std::weak_ptr<IMeshSystem> meshSys;
    std::weak_ptr<IMaterialSystem> matSys;
    std::weak_ptr<RendererAPI> renderer;
};

// Orchestrates multiple scenes
class SceneManager {
public:
    SceneManager(std::shared_ptr<IMeshSystem>, std::shared_ptr<IMaterialSystem>, std::shared_ptr<RendererAPI>);

    jaeng::result<Scene*> createScene(const std::string& name, std::unique_ptr<ISpatialPartitioner> partitioner, std::unique_ptr<ICamera> camera);

    void destroyScene(const std::string& name);

    Scene* getScene(const std::string& name);

private:
    std::unordered_map<std::string, std::unique_ptr<Scene>> scenes;
    std::weak_ptr<IMeshSystem> meshSys;
    std::weak_ptr<IMaterialSystem> matSys;
    std::weak_ptr<RendererAPI> renderer;
    std::unique_ptr<PipelineCache> pipelineCache;
};

} // namespace jaeng
