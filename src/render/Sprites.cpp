#include "render/Sprites.hpp"

#include <cmath>
#include <vector>

namespace pm {

void SpriteBank::load() {
    if (loaded_) return;
    int count = 0;
    const SpriteSource* sources = spriteSources(count);
    for (int s = 0; s < count; ++s) {
        const SpriteSource& src = sources[s];
        const int idx = static_cast<int>(src.id);
        const int sheetW = src.w * src.frames;
        std::vector<Color> color(static_cast<size_t>(sheetW) * src.h, Color{0, 0, 0, 0});
        std::vector<Color> glow(color.size(), Color{0, 0, 0, 0});
        std::vector<Color> sil(color.size(), Color{0, 0, 0, 0});
        bool anyGlow = false;
        for (int f = 0; f < src.frames; ++f) {
            for (int y = 0; y < src.h; ++y) {
                const char* row = src.rows[f * src.h + y];
                for (int x = 0; x < src.w && row[x] != '\0'; ++x) {
                    bool emissive = false;
                    Color c = spritePaletteColor(row[x], emissive);
                    if (c.a == 0) continue;
                    size_t i = static_cast<size_t>(y) * sheetW + f * src.w + x;
                    color[i] = c;
                    sil[i] = Color{255, 255, 255, 255};
                    if (emissive) {
                        glow[i] = c;
                        anyGlow = true;
                    }
                }
            }
        }
        auto bake = [&](std::vector<Color>& px) {
            Image img{px.data(), sheetW, src.h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
            Texture2D t = LoadTextureFromImage(img);
            SetTextureFilter(t, TEXTURE_FILTER_POINT);
            return t;
        };
        info_[idx] = {src.w, src.h, src.frames};
        color_[idx] = bake(color);
        silhouette_[idx] = bake(sil);
        hasEmissive_[idx] = anyGlow;
        if (anyGlow) emissive_[idx] = bake(glow);
    }
    loaded_ = true;
}

void SpriteBank::unload() {
    if (!loaded_) return;
    if (IsWindowReady()) {
        for (int i = 0; i < kCount; ++i) {
            if (color_[i].id) UnloadTexture(color_[i]);
            if (emissive_[i].id) UnloadTexture(emissive_[i]);
            if (silhouette_[i].id) UnloadTexture(silhouette_[i]);
        }
    }
    color_ = {};
    emissive_ = {};
    silhouette_ = {};
    loaded_ = false;
}

namespace {

void drawFrame(const Texture2D& tex, const SpriteInfo& info, int frame, Vector2 pos, bool flipX, Color tint) {
    if (tex.id == 0 || info.frames == 0) return;
    frame = ((frame % info.frames) + info.frames) % info.frames;
    Rectangle src{static_cast<float>(frame * info.w), 0.0f, static_cast<float>(flipX ? -info.w : info.w),
                  static_cast<float>(info.h)};
    // Snap to whole layer pixels so sprites stay aligned with the cell grid.
    Vector2 p{static_cast<float>(static_cast<int>(pos.x + (pos.x < 0 ? -0.5f : 0.5f))),
              static_cast<float>(static_cast<int>(pos.y + (pos.y < 0 ? -0.5f : 0.5f)))};
    DrawTextureRec(tex, src, p, tint);
}

} // namespace

void SpriteBank::draw(SpriteId id, int frame, Vector2 pos, bool flipX, Color tint) const {
    int i = static_cast<int>(id);
    drawFrame(color_[i], info_[i], frame, pos, flipX, tint);
}

void SpriteBank::drawEmissive(SpriteId id, int frame, Vector2 pos, bool flipX, Color tint) const {
    int i = static_cast<int>(id);
    if (hasEmissive_[i]) drawFrame(emissive_[i], info_[i], frame, pos, flipX, tint);
}

void SpriteBank::drawSilhouette(SpriteId id, int frame, Vector2 pos, bool flipX, Color tint) const {
    int i = static_cast<int>(id);
    drawFrame(silhouette_[i], info_[i], frame, pos, flipX, tint);
}

void SpriteBank::drawScaled(SpriteId id, int frame, Vector2 pos, int scale, bool flipX, Color tint, bool emissive,
                            bool silhouette) const {
    const int i = static_cast<int>(id);
    const SpriteInfo& in = info_[i];
    const Texture2D& tex = silhouette ? silhouette_[i] : (emissive ? emissive_[i] : color_[i]);
    if (tex.id == 0 || in.frames == 0) return;
    frame = ((frame % in.frames) + in.frames) % in.frames;
    Rectangle src{static_cast<float>(frame * in.w), 0.0f, static_cast<float>(flipX ? -in.w : in.w), static_cast<float>(in.h)};
    Rectangle dst{std::floor(pos.x), std::floor(pos.y), static_cast<float>(in.w * scale), static_cast<float>(in.h * scale)};
    DrawTexturePro(tex, src, dst, Vector2{0.0f, 0.0f}, 0.0f, tint);
}

void SpriteBank::drawRotated(SpriteId id, int frame, Vector2 center, float degrees, Color tint) const {
    int i = static_cast<int>(id);
    const SpriteInfo& in = info_[i];
    if (color_[i].id == 0 || in.frames == 0) return;
    frame = ((frame % in.frames) + in.frames) % in.frames;
    Rectangle src{static_cast<float>(frame * in.w), 0.0f, static_cast<float>(in.w), static_cast<float>(in.h)};
    Rectangle dst{center.x, center.y, static_cast<float>(in.w), static_cast<float>(in.h)};
    DrawTexturePro(color_[i], src, dst, Vector2{in.w * 0.5f, in.h * 0.5f}, degrees, tint);
}

} // namespace pm
