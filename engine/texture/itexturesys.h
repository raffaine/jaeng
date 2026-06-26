#pragma once

#include <string>
#include "common/result.h"
#include "common/async/task.h"
#include "render/public/renderer_api.h"

namespace jaeng {

using namespace renderer;

struct TextureImportDesc {
    TextureFormat format = TextureFormat::RGBA8_UNORM;
    bool generateMipmaps = true;
    SamplerFilter filter = SamplerFilter::Linear;
    AddressMode addressModeU = AddressMode::ClampToEdge;
    AddressMode addressModeV = AddressMode::ClampToEdge;
};

class ITextureSystem {
public:
    virtual ~ITextureSystem() = default;

    // Decodes PNG/JPG/TGA from virtual path and uploads to the GPU
    virtual result<TextureHandle> loadTexture(const std::string& path, const TextureImportDesc& desc = {}) = 0;
    virtual async::Task<result<TextureHandle>> loadTextureAsync(const std::string& path, const TextureImportDesc& desc = {}) = 0;

    virtual void unloadTexture(TextureHandle handle) = 0;
};

} // namespace jaeng
