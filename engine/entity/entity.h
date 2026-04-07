#pragma once

#include <vector>
#include <unordered_map>
#include <typeindex>
#include <memory>
#include <algorithm>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace jaeng {

using EntityID = uint32_t;

class IComponentPool {
public:
    virtual ~IComponentPool() = default;
    virtual void remove(EntityID id) = 0;
};

template<typename T>
class ComponentPool : public IComponentPool {
public:
    void remove(EntityID id) override {
        if (!contains(id)) return;

        size_t dense_index = sparse[id];
        size_t last_dense_index = dense.size() - 1;

        if (dense_index != last_dense_index) {
            T& last_comp = dense[last_dense_index];
            EntityID last_id = entities[last_dense_index];

            dense[dense_index] = std::move(last_comp);
            entities[dense_index] = last_id;
            sparse[last_id] = dense_index;
        }

        dense.pop_back();
        entities.pop_back();
        sparse[id] = static_cast<size_t>(-1);
    }

    bool contains(EntityID id) const {
        return id < sparse.size() && sparse[id] != static_cast<size_t>(-1);
    }

    T& operator[](EntityID id) {
        if (contains(id)) {
            return dense[sparse[id]];
        }

        if (id >= sparse.size()) {
            sparse.resize(id + 1, static_cast<size_t>(-1));
        }

        sparse[id] = dense.size();
        entities.push_back(id);
        dense.push_back(T{});
        return dense.back();
    }

    T* find(EntityID id) {
        if (contains(id)) {
            return &dense[sparse[id]];
        }
        return nullptr;
    }

    std::vector<T*> getAll() {
        std::vector<T*> v;
        v.reserve(dense.size());
        for (auto& d : dense) v.push_back(&d);
        return v;
    }

    const std::vector<EntityID>& getAllEntities() const {
        return entities;
    }

private:
    std::vector<size_t> sparse;
    std::vector<T> dense;
    std::vector<EntityID> entities;
};

struct Transform {
    glm::vec3 position = {0, 0, 0};
    glm::quat rotation = {1, 0, 0, 0};
    glm::vec3 scale    = {1, 1, 1};
};

struct WorldMatrix {
    glm::mat4 value{ 1.0f };
};

struct Relationship {
    EntityID parent = static_cast<EntityID>(-1);
    EntityID firstChild = static_cast<EntityID>(-1);
    EntityID nextSibling = static_cast<EntityID>(-1);
    EntityID prevSibling = static_cast<EntityID>(-1);
};

struct CameraComponent {
    float fov = 60.0f;
    float aspect = 1.77f;
    float znear = 0.1f;
    float zfar = 1000.0f;
    
    // Internal Euler angles for spherical control
    float yaw = 0.0f;
    float pitch = 0.0f;
};

class EntityManager {
public:
    EntityManager() = default;
    EntityManager(const EntityManager&) = delete;
    EntityManager& operator=(const EntityManager&) = delete;
    EntityManager(EntityManager&&) noexcept = default;
    EntityManager& operator=(EntityManager&&) noexcept = default;

    EntityID createEntity() {
        EntityID id = nextID++;
        entities.push_back(id);
        return id;
    }

    template<typename T>
    T& addComponent(EntityID id) {
        return getPool<T>()[id];
    }

    template<typename T>
    T* getComponent(EntityID id) {
        return getPool<T>().find(id);
    }
    
    template<typename T>
    std::vector<T*> getAllComponents() {
        return getPool<T>().getAll();
    }

    template<typename T>
    const std::vector<EntityID>& getAllEntities() {
        return getPool<T>().getAllEntities();
    }

    void destroyEntity(EntityID id) {
        entities.erase(std::remove(entities.begin(), entities.end(), id), entities.end());
        for (auto& [type, pool] : pools) {
            pool->remove(id);
        }
    }

    inline void attachEntity(EntityID child, EntityID parent);

private:
    EntityID nextID = 1;
    std::vector<EntityID> entities;
    std::unordered_map<std::type_index, std::unique_ptr<IComponentPool>> pools;

    template<typename T>
    ComponentPool<T>& getPool() {
        auto it = pools.find(typeid(T));
        if (it == pools.end()) {
            it = pools.emplace(typeid(T), std::make_unique<ComponentPool<T>>()).first;
        }
        return static_cast<ComponentPool<T>&>(*it->second);
    }
};

inline void EntityManager::attachEntity(EntityID child, EntityID parent) {
    auto* childRel = getComponent<Relationship>(child);
    if (!childRel) childRel = &(addComponent<Relationship>(child));
    childRel->parent = parent;

    auto* parentRel = getComponent<Relationship>(parent);
    if (!parentRel) parentRel = &(addComponent<Relationship>(parent));

    if (parentRel->firstChild != static_cast<EntityID>(-1)) {
        auto* firstChildRel = getComponent<Relationship>(parentRel->firstChild);
        firstChildRel->prevSibling = child;
        childRel->nextSibling = parentRel->firstChild;
    }
    parentRel->firstChild = child;
}

} // namespace jaeng
