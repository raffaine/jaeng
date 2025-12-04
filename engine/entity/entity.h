#pragma once

#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

using EntityID = uint32_t;

class IComponentPool {
public:
    virtual void remove(EntityID id) = 0;
};

template<typename T>
class ComponentPool : public IComponentPool {
public:
    void remove(EntityID id) override {
        data.erase(id);
    }

    T& operator[](EntityID id) {
        return data[id];
    }

    T* find(EntityID id) {
        if (auto it = data.find(id); it != data.end()) {
            return &(it->second);
        }
        return nullptr;
    }

    std::vector<T*> getAll() {
        std::vector<T*> v;
        v.reserve(data.size());
        for (auto& [k,d] : data) v.push_back(&d);
        return v;
    }

    std::vector<EntityID> getAllEntities() {
        std::vector<EntityID> v;
        for (auto& [k,d] : data) v.push_back(k);
        return v;
    }
private:
    std::unordered_map<EntityID, T> data;
};

struct Transform {
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 scale;
};

// struct MeshComponent {
//     MeshHandle mesh;
// };

// struct MaterialComponent {
//     MaterialHandle material;
// };

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
        auto& pool = getPool<T>();
        return pool[id]; // Creates or returns existing
    }

    template<typename T>
    T* getComponent(EntityID id) {
        auto& pool = getPool<T>();
        return pool.find(id);
    }
    
    template<typename T>
    std::vector<T*> getAllComponents() {
        return getPool<T>().getAll();
    }

    template<typename T>
    std::vector<EntityID> getAllEntities() {
        return getPool<T>().getAllEntities();
    }

    void destroyEntity(EntityID id) {
        entities.erase(std::remove(entities.begin(), entities.end(), id), entities.end());
        for (auto* pool : pools) {
            pool->remove(id);
        }
    }

private:
    EntityID nextID = 1;
    std::vector<EntityID> entities;
    std::vector<IComponentPool*> pools;

    template<typename T>
    ComponentPool<T>& getPool() {
        static ComponentPool<T> pool;
        if (std::find(pools.begin(), pools.end(), &pool) == pools.end()) {
            pools.push_back(&pool);
        }

        return pool;
    }
};
