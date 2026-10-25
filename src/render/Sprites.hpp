#pragma once
// Pixel-art sprites authored as ASCII art in SpriteData.cpp and baked into textures at
// startup. Each sprite has a colour sheet, an emissive sheet (glowing pixels such as eyes)
// and a white silhouette sheet used for hit flashes. Frames are laid out horizontally.

#include <array>
#include <cstdint>

#include "raylib.h"

namespace pm {

enum class SpriteId : uint8_t {
    PlayerIdle,
    PlayerRun,
    PlayerAir,
    Slime,
    Bat,
    Ghoul,
    Imp,
    Wraith,
    Shroom,
    Turret,
    Beetle,
    WormHead,
    WormBody,
    Golem,
    Homunculus,
    Lantern,
    Chest,
    ChestOpen,
    Potion,
    SpellCard,
    Portal,
    Altar,
    Pedestal,
    Boss,
    BossCore,
    Mimic,
    BileSlug,
    Count
};

struct SpriteInfo {
    int w = 0, h = 0, frames = 0;
};

class SpriteBank {
public:
    /// Bakes every sprite. Requires an active window / GL context.
    void load();
    void unload();
    ~SpriteBank() { unload(); }

    const SpriteInfo& info(SpriteId id) const { return info_[static_cast<int>(id)]; }

    /// Draws a frame with its top-left corner at `pos` (layer pixel coordinates).
    void draw(SpriteId id, int frame, Vector2 pos, bool flipX, Color tint = WHITE) const;
    void drawEmissive(SpriteId id, int frame, Vector2 pos, bool flipX, Color tint = WHITE) const;
    void drawSilhouette(SpriteId id, int frame, Vector2 pos, bool flipX, Color tint) const;
    /// Draws a frame scaled by an integer factor (top-left at `pos`); used for giants.
    void drawScaled(SpriteId id, int frame, Vector2 pos, int scale, bool flipX, Color tint = WHITE, bool emissive = false,
                    bool silhouette = false) const;
    /// Draws centred at `center`, rotated by `degrees` (used for spinning items).
    void drawRotated(SpriteId id, int frame, Vector2 center, float degrees, Color tint = WHITE) const;

    bool hasEmissive(SpriteId id) const { return hasEmissive_[static_cast<int>(id)]; }

private:
    static constexpr int kCount = static_cast<int>(SpriteId::Count);
    std::array<SpriteInfo, kCount> info_{};
    std::array<Texture2D, kCount> color_{};
    std::array<Texture2D, kCount> emissive_{};
    std::array<Texture2D, kCount> silhouette_{};
    std::array<bool, kCount> hasEmissive_{};
    bool loaded_ = false;
};

/// ASCII source for a sprite: `frames` frames of `h` rows of `w` characters, stacked
/// vertically in `rows`. See SpriteData.cpp for the palette legend.
struct SpriteSource {
    SpriteId id;
    int w, h, frames;
    const char* const* rows;
};

/// Returns the table of all sprite sources (defined in SpriteData.cpp).
const SpriteSource* spriteSources(int& count);

/// Palette lookup for a sprite character; `emissive` is set for glowing pixels.
Color spritePaletteColor(char c, bool& emissive);

} // namespace pm
