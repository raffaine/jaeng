#pragma once

#include "itexturesys.h"
#include "storage/ifstorage.h"
#include <memory>

namespace jaeng {

class TextureSystem : public ITextureSystem {
public:
    TextureSystem(std::shared_ptr<IFileManager> fm, std::weak_ptr<renderer::RendererAPI> renderer);
    ~TextureSystem() override;

    result<TextureHandle> loadTexture(const std::string& path, const TextureImportDesc& desc = {}) override;
    async::Task<result<TextureHandle>> loadTextureAsync(const std::string& path, const TextureImportDesc& desc = {}) override;

    void unloadTexture(TextureHandle handle) override;

private:
    std::shared_ptr<IFileManager> fm_;
    std::weak_ptr<renderer::RendererAPI> renderer_;
};

} // namespace jaeng
