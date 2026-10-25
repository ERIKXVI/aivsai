#include "game/Pickup.hpp"

namespace pm {

namespace {

Pickup base(PickupType type, Vec2 pos, float halfW, float halfH) {
    Pickup p;
    p.type = type;
    p.body.pos = p.body.prevPos = pos;
    p.body.halfW = halfW;
    p.body.halfH = halfH;
    p.body.stepHeight = 0.0f;
    p.body.friction = 300.0f;
    p.body.bounce = 0.25f;
    return p;
}

} // namespace

std::string Pickup::label() const {
    std::string text;
    switch (type) {
    case PickupType::Spell: text = spellDef(spell).name; break;
    case PickupType::Wand: text = wand ? wand->name : "Wand"; break;
    case PickupType::Potion: text = std::string("Flask of ") + matDef(mat).name; break;
    case PickupType::Chest: text = "Chest"; break;
    case PickupType::PerkAltar: text = perkDef(perk).name; break;
    default: break;
    }
    if (price > 0) text += "  (" + std::to_string(price) + " gold)";
    return text;
}

Pickup makeGold(Vec2 pos, int value, Vec2 vel) {
    Pickup p = base(PickupType::Gold, pos, 1.0f, 1.0f);
    p.amount = value;
    p.body.vel = vel;
    p.body.bounce = 0.45f;
    return p;
}

Pickup makeSpellPickup(Vec2 pos, SpellId spell) {
    Pickup p = base(PickupType::Spell, pos, 3.0f, 4.0f);
    p.spell = spell;
    return p;
}

Pickup makeWandPickup(Vec2 pos, Wand wand) {
    Pickup p = base(PickupType::Wand, pos, 4.0f, 2.0f);
    p.wand = std::move(wand);
    return p;
}

Pickup makePotion(Vec2 pos, MatId mat, int amount) {
    Pickup p = base(PickupType::Potion, pos, 3.0f, 4.0f);
    p.mat = mat;
    p.amount = amount;
    return p;
}

Pickup makeChest(Vec2 pos) {
    Pickup p = base(PickupType::Chest, pos, 6.0f, 4.0f);
    p.body.bounce = 0.0f;
    return p;
}

Pickup makeHeart(Vec2 pos) {
    Pickup p = base(PickupType::Heart, pos, 2.0f, 2.0f);
    p.amount = 25;
    return p;
}

Pickup makePerkAltar(Vec2 pos, PerkId perk, int group) {
    Pickup p = base(PickupType::PerkAltar, pos, 6.0f, 5.0f);
    p.perk = perk;
    p.altarGroup = group;
    p.body.gravityScale = 0.0f;
    return p;
}

MatId randomPotionMaterial(int tier, Rng& rng) {
    struct Entry {
        MatId mat;
        float weight;
        int minTier;
    };
    static constexpr Entry kTable[] = {
        {mat::Water, 3.0f, 0}, {mat::Vitae, 2.5f, 0}, {mat::Oil, 1.5f, 0},  {mat::Acid, 1.2f, 1},
        {mat::Slime, 0.6f, 0}, {mat::Toxic, 0.7f, 1}, {mat::Lava, 0.8f, 2}, {mat::Quicksilver, 0.7f, 0},
        {mat::Midas, 0.25f, 2}, {mat::Blood, 0.5f, 0}, {mat::Ichor, 0.5f, 1},
    };
    float total = 0.0f;
    for (const Entry& e : kTable)
        if (e.minTier <= tier) total += e.weight;
    float r = rng.uniform() * total;
    for (const Entry& e : kTable) {
        if (e.minTier > tier) continue;
        if (r < e.weight) return e.mat;
        r -= e.weight;
    }
    return mat::Water;
}

} // namespace pm
