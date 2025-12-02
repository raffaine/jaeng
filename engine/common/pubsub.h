#pragma once

#include <functional>
#include <unordered_map>
#include <vector>
#include <typeindex>
#include <memory>

class EventBus : public std::enable_shared_from_this<EventBus> {
public:
    template<typename Event>
    class Subscription {
    public:
        Subscription(std::shared_ptr<EventBus> publisher, size_t id)
            : bus(publisher), id(id) {}
        ~Subscription() {
            if (auto b = bus.lock()) b->unsubscribe<Event>(id);
        }
    private:
        std::weak_ptr<EventBus> bus;
        size_t id;
    };

    template<typename Event>
    std::unique_ptr<Subscription<Event>> subscribe(std::function<void(const Event&)> callback) {
        auto& vec = listeners[typeid(Event)];
        size_t id = nextId++;
        vec.push_back({id, [callback](const void* e) {
            callback(*static_cast<const Event*>(e));
        }});
        return std::make_unique<Subscription<Event>>(shared_from_this(), id);
    }

    template<typename Event>
    void publish(const Event& event) {
        auto it = listeners.find(typeid(Event));
        if (it != listeners.end()) {
            for (auto& [id, fn] : it->second) {
                fn(&event);
            }
        }
    }

private:
    struct Listener {
        size_t id;
        std::function<void(const void*)> fn;
    };

    std::unordered_map<std::type_index, std::vector<Listener>> listeners;
    size_t nextId = 0;

    template<typename Event>
    void unsubscribe(size_t id) {
        auto it = listeners.find(typeid(Event));
        if (it != listeners.end()) {
            auto& vec = it->second;
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                [id](const Listener& l) { return l.id == id; }), vec.end());
        }
    }
};
