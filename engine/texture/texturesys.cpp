#include "texturesys.h"
#include "common/logging.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace jaeng {

TextureSystem::TextureSystem(std::shared_ptr<IFileManager> fm, std::weak_ptr<renderer::RendererAPI> renderer)
    : fm_(std::move(fm)), renderer_(std::move(renderer)) {}

TextureSystem::~TextureSystem() {}

result<TextureHandle> TextureSystem::loadTexture(const std::string& path, const TextureImportDesc& desc) {
    auto gfx = renderer_.lock();
    JAENG_ERROR_IF(!gfx, error_code::resource_not_ready, "Renderer missing");

    JAENG_TRY_ASSIGN(auto fileData, fm_->load(path));

    int width, height, channels;
    // Force 4 channels (RGBA)
    stbi_uc* pixels = stbi_load_from_memory(
        reinterpret_cast<const stbi_uc*>(fileData.data()),
        static_cast<int>(fileData.size()),
        &width, &height, &channels, 4
    );

    JAENG_ERROR_IF(!pixels, error_code::invalid_args, "Failed to decode image");

    renderer::TextureDesc td {
        desc.format,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        1, // depth
        desc.generateMipmaps ? 0u : 1u, // mipLevels (0 = auto)
        0 // flags
    };

    TextureHandle handle = gfx->create_texture(&td, pixels);
    stbi_image_free(pixels);

    return handle;
}

async::Task<result<TextureHandle>> TextureSystem::loadTextureAsync(const std::string& path, const TextureImportDesc& desc) {
    // In a real scenario, decompression should happen on a worker thread.
    // For now we wrap the synchronous load.
    co_return loadTexture(path, desc);
}

void TextureSystem::unloadTexture(TextureHandle handle) {
    if (auto gfx = renderer_.lock()) {
        gfx->destroy_texture(handle);
    }
}

} // namespace jaeng
