#include "fontsys.h"
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#include "common/logging.h"

namespace jaeng {

FontSystem::FontSystem(std::shared_ptr<IFileManager> fm, std::shared_ptr<RendererAPI> renderer)
    : fileManager(fm), renderer(renderer) {}

FontSystem::~FontSystem() {
    auto gfx = renderer.lock();
    if (gfx) {
        for (auto& f : fonts) {
            if (f->texture) {
                gfx->destroy_texture(f->texture);
            }
        }
    }
}

jaeng::result<FontHandle> FontSystem::loadFont(const std::string& path, float pixelHeight) {
    auto fm = fileManager.lock();
    auto gfx = renderer.lock();
    JAENG_ERROR_IF(!fm || !gfx, jaeng::error_code::resource_not_ready, "[Font] FileManager or Renderer unavailable.");

    JAENG_TRY_ASSIGN(auto ttf_data, fm->load(path));

    auto fontData = std::make_unique<FontData>();
    fontData->pixelHeight = pixelHeight;
    fontData->atlasSize = 512;
    fontData->cdata.resize(96); // ASCII 32..126

    std::vector<uint8_t> temp_bitmap(fontData->atlasSize * fontData->atlasSize);

    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, ttf_data.data(), 0)) {
        JAENG_ERROR_IF(true, jaeng::error_code::unknown_error, "[Font] Failed to init font info.");
    }
    
    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&info, &ascent, &descent, &lineGap);
    float scale = stbtt_ScaleForPixelHeight(&info, pixelHeight);
    fontData->ascent = ascent * scale;
    fontData->descent = descent * scale;
    fontData->lineGap = lineGap * scale;

    std::vector<stbtt_bakedchar> baked_cdata(96);
    int res = stbtt_BakeFontBitmap(ttf_data.data(), 0, pixelHeight, temp_bitmap.data(), fontData->atlasSize, fontData->atlasSize, 32, 96, baked_cdata.data());
    JAENG_ERROR_IF(res <= 0, jaeng::error_code::unknown_error, "[Font] Failed to bake font bitmap.");

    fontData->cdata.resize(96);
    for (int i = 0; i < 96; ++i) {
        fontData->cdata[i].x0 = baked_cdata[i].x0;
        fontData->cdata[i].y0 = baked_cdata[i].y0;
        fontData->cdata[i].x1 = baked_cdata[i].x1;
        fontData->cdata[i].y1 = baked_cdata[i].y1;
        fontData->cdata[i].xoff = baked_cdata[i].xoff;
        fontData->cdata[i].yoff = baked_cdata[i].yoff;
        fontData->cdata[i].xadvance = baked_cdata[i].xadvance;
    }

    // Convert 8-bit alpha to 32-bit RGBA
    std::vector<uint32_t> rgba_bitmap(fontData->atlasSize * fontData->atlasSize);
    for (size_t i = 0; i < temp_bitmap.size(); ++i) {
        uint8_t a = temp_bitmap[i];
        rgba_bitmap[i] = (a << 24) | (255 << 16) | (255 << 8) | 255;
    }

    TextureDesc td { TextureFormat::RGBA8_UNORM, fontData->atlasSize, fontData->atlasSize, 1, 1, 0 };
    fontData->texture = gfx->create_texture(&td, rgba_bitmap.data());

    FontHandle handle = static_cast<FontHandle>(fonts.size());
    fonts.push_back(std::move(fontData));

    return handle;
}

jaeng::result<const FontData*> FontSystem::getFont(FontHandle handle) const {
    JAENG_ERROR_IF(handle >= fonts.size(), jaeng::error_code::no_resource, "[Font] Invalid font handle.");
    return fonts[handle].get();
}

} // namespace jaeng