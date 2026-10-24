#include "game/LevelRenderer.hpp"

#include <algorithm>
#include <cmath>

#include "game/Level.hpp"
#include "render/SceneRenderer.hpp"
#include "render/Sprites.hpp"

namespace pm {

namespace {

Color hexColor(uint32_t hex, uint8_t a = 255) {
    return Color{static_cast<unsigned char>((hex >> 16) & 0xFF), static_cast<unsigned char>((hex >> 8) & 0xFF),
                 static_cast<unsigned char>(hex & 0xFF), a};
}

Color spellColor(SpellId s) {
    switch (spellDef(s).type) {
    case SpellType::Projectile: return hexColor(spellDef(s).proj.color);
    case SpellType::Modifier: return Color{120, 230, 140, 255};
    case SpellType::Multicast: return Color{255, 220, 110, 255};
    }
    return WHITE;
}


} // namespace

void LevelRenderer::render(const Level& level, Vec2 camera, float alpha, float time, int screenW, int screenH) {
    scene_.beginFrame(camera, screenW, screenH);
    const ViewInfo& v = scene_.view();
    const World& world = level.world();

    WorldRenderer& layer = scene_.worldLayer();
    layer.build(world, v.originX, v.originY, world.tick());
    level.particles().draw(layer);
    DrawContext ctx{sprites_, scene_, alpha, time};
    for (const Projectile& p : level.projectiles()) drawProjectile(p, ctx, level.ticks());
    layer.upload();

    background_.setBiome(level.biome(), level.layout().seed);

    // Albedo.
    scene_.beginAlbedo();
    ClearBackground(BLACK);
    background_.draw(v.originX, v.originY, v.texW, v.texH);
    scene_.drawWorldAlbedo();
    drawProps(level, time, false);
    for (const auto& e : level.enemies())
        if (e->alive()) e->drawAlbedo(ctx);
    if (level.player().alive()) level.player().drawAlbedo(ctx);
    scene_.endAlbedo();

    // Emission.
    scene_.beginEmission();
    drawProps(level, time, true);
    for (const auto& e : level.enemies())
        if (e->alive()) e->drawEmission(ctx);
    if (level.player().alive()) level.player().drawEmission(ctx);
    scene_.endEmission();

    addLights(level, alpha, time);

    const BiomeDef& b = biomeDef(level.biome());
    LightingParams lp;
    lp.ambientR = b.ambient[0];
    lp.ambientG = b.ambient[1];
    lp.ambientB = b.ambient[2];
    lp.tintR = b.tint[0];
    lp.tintG = b.tint[1];
    lp.tintB = b.tint[2];
    lp.saturation = b.saturation;
    lp.damageFlash = level.playerHurtPulse() * 0.7f;
    const Player& pl = level.player();
    if (pl.alive() && pl.hp < pl.maxHp * 0.25f) lp.damageFlash = std::max(lp.damageFlash, 0.25f + 0.2f * std::sin(time * 6.0f));
    scene_.present(lp, time);
}

void LevelRenderer::drawProps(const Level& level, float time, bool emissive) {
    // Lanterns hanging on chains.
    for (const Lantern& l : level.lanterns()) {
        const SpriteInfo& info = sprites_.info(SpriteId::Lantern);
        Vector2 p = scene_.layerPos({l.pos.x - info.w * 0.5f, l.pos.y - 2.0f});
        int frame = static_cast<int>(time * 5.0f + l.phase) % 2;
        if (emissive) sprites_.drawEmissive(SpriteId::Lantern, frame, p, false);
        else sprites_.draw(SpriteId::Lantern, frame, p, false);
    }

    // The exit portal; sealed while a guardian lives.
    {
        const bool sealed = level.boss() != nullptr;
        const SpriteInfo& info = sprites_.info(SpriteId::Portal);
        Vec2 c = level.exitPos() - Vec2{0.0f, 9.0f + std::sin(time * 2.0f)};
        Vector2 p = scene_.layerPos({c.x - info.w * 0.5f, c.y - info.h * 0.5f});
        int frame = static_cast<int>(time * 6.0f) % 2;
        Color tint = sealed ? Color{90, 80, 100, 255} : WHITE;
        if (emissive) {
            if (!sealed) sprites_.drawEmissive(SpriteId::Portal, frame, p, (static_cast<int>(time * 3.0f) % 2) == 0, tint);
        } else {
            sprites_.draw(SpriteId::Portal, frame, p, false, tint);
        }
    }

    for (const Pickup& pk : level.pickups()) {
        const Vec2 pos = lerp(pk.body.prevPos, pk.body.pos, 0.0f);
        const float bob = std::sin(time * 3.0f + pos.x * 0.1f) * 1.5f;
        if (pk.price > 0 || level.isSanctum()) {
            if (pk.type != PickupType::PerkAltar) {
                const SpriteInfo& pi = sprites_.info(SpriteId::Pedestal);
                Vector2 pp = scene_.layerPos({pos.x - pi.w * 0.5f, pos.y + 6.0f});
                if (!emissive) sprites_.draw(SpriteId::Pedestal, 0, pp, false);
            }
        }
        switch (pk.type) {
        case PickupType::Gold: {
            Vector2 p = scene_.layerPos({pos.x - 1.0f, pos.y - 1.0f});
            int x = static_cast<int>(std::floor(p.x)), y = static_cast<int>(std::floor(p.y));
            bool glint = static_cast<int>(time * 4.0f + pos.x) % 6 == 0;
            if (emissive) {
                DrawPixel(x, y, glint ? Color{255, 240, 180, 255} : Color{120, 90, 20, 255});
            } else {
                DrawRectangle(x, y, 2, 2, Color{230, 180, 50, 255});
                DrawPixel(x, y, Color{255, 240, 160, 255});
            }
            break;
        }
        case PickupType::Spell: {
            const SpriteInfo& info = sprites_.info(SpriteId::SpellCard);
            Vector2 p = scene_.layerPos({pos.x - info.w * 0.5f, pos.y - info.h * 0.5f + bob});
            Color c = spellColor(pk.spell);
            if (emissive) sprites_.drawEmissive(SpriteId::SpellCard, 0, p, false, Color{c.r, c.g, c.b, 160});
            else sprites_.draw(SpriteId::SpellCard, 0, p, false, c);
            break;
        }
        case PickupType::Wand: {
            if (!pk.wand) break;
            Vec2 base = pos + Vec2{-static_cast<float>(pk.wand->length) * 0.5f, bob};
            for (int i = 0; i <= pk.wand->length; ++i) {
                Vector2 p = scene_.layerPos(base + Vec2{static_cast<float>(i), -static_cast<float>(i) * 0.35f});
                bool gem = i >= pk.wand->length - 1;
                if (emissive && !gem) continue;
                DrawPixel(static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)),
                          hexColor(gem ? pk.wand->gemColor : pk.wand->handleColor));
            }
            break;
        }
        case PickupType::Potion: {
            const SpriteInfo& info = sprites_.info(SpriteId::Potion);
            Vector2 p = scene_.layerPos({pos.x - info.w * 0.5f, pos.y + pk.body.halfH - static_cast<float>(info.h)});
            Rgba lc = cellColor(makeCell(pk.mat, 128), 0, 0, 0);
            Color tint{lc.r, lc.g, lc.b, 255};
            if (emissive) {
                const MaterialDef& md = matDef(pk.mat);
                if (md.glow.a > 0) sprites_.drawSilhouette(SpriteId::Potion, 0, p, false, Color{md.glow.r, md.glow.g, md.glow.b, 70});
            } else {
                sprites_.draw(SpriteId::Potion, 0, p, false, tint);
            }
            break;
        }
        case PickupType::Heart: {
            Vector2 p = scene_.layerPos({pos.x - 2.0f, pos.y - 2.0f + bob});
            int x = static_cast<int>(std::floor(p.x)), y = static_cast<int>(std::floor(p.y));
            Color c = emissive ? Color{160, 30, 50, 255} : Color{230, 50, 70, 255};
            DrawRectangle(x, y, 2, 2, c);
            DrawRectangle(x + 3, y, 2, 2, c);
            DrawRectangle(x, y + 1, 5, 2, c);
            DrawRectangle(x + 1, y + 3, 3, 1, c);
            DrawPixel(x + 2, y + 4, c);
            break;
        }
        case PickupType::Chest: {
            SpriteId s = pk.opened ? SpriteId::ChestOpen : SpriteId::Chest;
            const SpriteInfo& info = sprites_.info(s);
            Vector2 p = scene_.layerPos({pos.x - info.w * 0.5f, pos.y + pk.body.halfH - static_cast<float>(info.h)});
            if (emissive) sprites_.drawEmissive(s, 0, p, false);
            else sprites_.draw(s, 0, p, false);
            break;
        }
        case PickupType::PerkAltar: {
            const SpriteInfo& info = sprites_.info(SpriteId::Altar);
            Vector2 p = scene_.layerPos({pos.x - info.w * 0.5f, pos.y + pk.body.halfH - static_cast<float>(info.h)});
            Color c = hexColor(perkDef(pk.perk).color);
            if (emissive) {
                sprites_.drawEmissive(SpriteId::Altar, 0, p, false, c);
                Vector2 orb = scene_.layerPos({pos.x, pos.y - 12.0f + bob});
                DrawCircleV(orb, 2.5f, c);
            } else {
                sprites_.draw(SpriteId::Altar, 0, p, false);
                Vector2 orb = scene_.layerPos({pos.x, pos.y - 12.0f + bob});
                DrawCircleV(orb, 2.5f, c);
                DrawPixel(static_cast<int>(orb.x), static_cast<int>(orb.y) - 1, WHITE);
            }
            break;
        }
        }
    }
}

void LevelRenderer::addLights(const Level& level, float alpha, float time) {
    DrawContext ctx{sprites_, scene_, alpha, time};
    if (level.player().alive()) level.player().addLights(ctx);

    for (const LightFlash& f : level.flashes()) {
        float k = f.life / f.maxLife;
        scene_.addLight(PointLight{f.pos, f.r * k, f.g * k, f.b * k, f.radius, 2.0f});
    }

    // The exit portal is a beacon.
    if (!level.boss()) {
        float pulse = 1.0f + 0.25f * std::sin(time * 3.0f);
        scene_.addLight(PointLight{level.exitPos() - Vec2{0.0f, 9.0f}, 1.2f * pulse, 0.7f * pulse, 2.0f * pulse, 110.0f, 7.0f});
    }

    for (const auto& e : level.enemies())
        if (e->alive()) e->addLights(ctx);

    int projLights = 0;
    for (const Projectile& p : level.projectiles()) {
        if (projLights++ > 24) break;
        Color c = hexColor(p.def.color);
        float k = p.def.flags & projflag::kHitscan ? 3.0f : 1.4f;
        Vec2 pos = (p.def.flags & projflag::kHitscan) ? lerp(p.pos, p.beamEnd, 0.5f) : lerp(p.prevPos, p.pos, alpha);
        scene_.addLight(PointLight{pos, c.r / 255.0f * k, c.g / 255.0f * k, c.b / 255.0f * k, p.def.lightRadius, 1.5f});
    }

    for (const Lantern& l : level.lanterns()) {
        float flicker = 0.85f + 0.15f * std::sin(time * 13.0f + l.phase) * std::sin(time * 7.3f + l.phase * 2.0f);
        scene_.addLight(PointLight{l.pos + Vec2{0.0f, 3.0f}, 1.6f * flicker, 1.1f * flicker, 0.55f * flicker, 85.0f, 4.0f});
    }

    for (const Pickup& pk : level.pickups()) {
        if (pk.type == PickupType::PerkAltar) {
            Color c = hexColor(perkDef(pk.perk).color);
            scene_.addLight(PointLight{pk.body.pos - Vec2{0.0f, 12.0f}, c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, 45.0f, 3.0f});
        }
    }

    // Brightest emissive regions (lava, fires, glowing fungus) cast shadowed light.
    scene_.addEmissiveLights(18);
}

} // namespace pm
