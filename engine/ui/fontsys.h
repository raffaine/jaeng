#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include "common/result.h"
#include "render/public/renderer_api.h"
#include "storage/ifstorage.h"

namespace jaeng {

typedef uint32_t FontHandle;

struct GlyphData {
    unsigned short x0, y0, x1, y1; // coordinates of bbox in bitmap
    float xoff, yoff, xadvance;
};

struct FontData {
    TextureHandle texture = 0;
    std::vector<GlyphData> cdata;
    float pixelHeight = 32.0f;
    uint32_t atlasSize = 512;
    float ascent = 0;
    float descent = 0;
    float lineGap = 0;
};

class IFontSystem {
public:
    virtual ~IFontSystem() = default;
    virtual jaeng::result<FontHandle> loadFont(const std::string& path, float pixelHeight) = 0;
    virtual jaeng::result<const FontData*> getFont(FontHandle handle) const = 0;
};

class FontSystem : public IFontSystem {
public:
    FontSystem(IFileManager& fm, std::shared_ptr<RendererAPI> renderer);
    ~FontSystem();

    jaeng::result<FontHandle> loadFont(const std::string& path, float pixelHeight) override;
    jaeng::result<const FontData*> getFont(FontHandle handle) const override;

private:
    IFileManager* fileManager;
    std::weak_ptr<RendererAPI>  renderer;
    
    std::vector<std::unique_ptr<FontData>> fonts;
};

} // namespace jaeng
