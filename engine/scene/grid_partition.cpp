#include "grid_partition.h"

namespace jaeng {
    void GridPartitioner::addOrUpdate(const RenderProxy& proxy) {
        proxies_[proxy.id] = proxy;
    }

    void GridPartitioner::remove(uint32_t id) {
        proxies_.erase(id);
    }

    void GridPartitioner::build() {}
    void GridPartitioner::rebuild() {}
    void GridPartitioner::reset() { 
        proxies_.clear(); 
    }

    std::vector<RenderProxy> GridPartitioner::queryVisible(const jaeng::math::AABB& /*volume*/) const {
        std::vector<RenderProxy> result;
        result.reserve(proxies_.size());
        for (const auto& [id, proxy] : proxies_) {
            result.push_back(proxy);
        }
        return result;
    }
};
