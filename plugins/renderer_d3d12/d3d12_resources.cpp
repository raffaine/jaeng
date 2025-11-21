#include "d3d12_resources.h"

template <typename T>
static auto push_handle(std::vector<T>& vec, T&& v) {
    vec.push_back(std::move(v));
    return vec.size();
}

template <typename T, typename HandleT>
static T* get_handle(std::vector<T>& vec, HandleT h) {
    if (h == 0 || h > vec.size()) return nullptr;
    size_t idx = size_t(h - 1);
    return &vec[idx];
}

BufferHandle  ResourceTable::add_buffer(BufferRec&& b) {
    return (BufferHandle)push_handle(buffers_, std::move(b));
}

TextureHandle ResourceTable::add_texture(TextureRec&& t){
    return (TextureHandle)push_handle(textures_, std::move(t));
}

SamplerHandle ResourceTable::add_sampler(SamplerRec&& s){
    return (SamplerHandle)push_handle(samplers_, std::move(s));
}

BufferRec*  ResourceTable::get_buf(BufferHandle h) {
    return get_handle(buffers_, h);
}

TextureRec* ResourceTable::get_tex(TextureHandle h) {
    return get_handle(textures_, h);
}

SamplerRec* ResourceTable::get_samp(SamplerHandle h) {
    return get_handle(samplers_, h);
}
