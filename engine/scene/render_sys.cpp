#include "render_sys.h"
#include "entity/transform_sys.h"

#include "scene.h"

namespace jaeng {

void SceneRenderSystem::extract(Scene& scene, EntityManager& ecs, std::vector<RenderCommand>& outCommands, 
                                std::function<void(EntityID, RenderProxy&)> visitor,
                                const math::AABB* volume) {
    const auto& entities = ecs.getAllEntities<WorldMatrix>();
    
    for (auto e : entities) {
        auto* wm = ecs.getComponent<WorldMatrix>(e);
        auto* mesh = ecs.getComponent<MeshComponent>(e);
        auto* mat = ecs.getComponent<MaterialComponent>(e);
        auto* cb = ecs.getComponent<BufferComponent>(e);

        if (wm && mesh && mat) {
            // Optional spatial filtering
            if (volume) {
                glm::vec3 pos = glm::vec3(wm->value[3]);
                if (!volume->contains(pos)) continue;
            }

            RenderCommand cmd;
            cmd.type = RenderCommandType::Update;
            
            cmd.proxy = RenderProxy { 
                static_cast<uint32_t>(e), 
                wm->value, 
                mesh->handle, 
                mat->handle, 
                cb ? cb->handle : 0, 
                glm::vec4(1.0f) // default color
            };

            // Allow the visitor to modify the proxy before queuing
            if (visitor) {
                visitor(e, cmd.proxy);
            }

            outCommands.push_back(cmd);
        }
    }
}

} // namespace jaeng
