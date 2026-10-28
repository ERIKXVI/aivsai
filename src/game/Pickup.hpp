#pragma once
// Things lying in the world: gold, spell cards, wands, potions, chests, shop wares, perk
// altars and healing hearts.

#include <optional>
#include <string>

#include "core/Rng.hpp"
#include "game/Body.hpp"
#include "game/Perks.hpp"
#include "game/Wand.hpp"

namespace pm {

enum class PickupType : uint8_t { Gold, Spell, Wand, Potion, Heart, Chest, PerkAltar };

struct Pickup {
    PickupType type = PickupType::Gold;
    Body body;
    SpellId spell = SpellId::None;
    std::optional<Wand> wand;
    MatId mat = mat::Empty; ///< potion contents
    int amount = 0;         ///< gold value / potion volume
    int price = 0;          ///< > 0 when sold in a shop
    PerkId perk = PerkId::Vitality;
    int altarGroup = -1;    ///< perk altars in the same group vanish together
    uint32_t lootSeed = 0;  ///< a chest's treasure is rolled from this (see Level::dropLoot)
    float age = 0.0f;
    bool opened = false;
    bool rare = false;      ///< a rare spell find: glints gold and is announced
    bool setDown = false;   ///< set down by the alchemist from the satchel (see Level::focusedPickup)
    bool dead = false;

    bool interactive() const { return type != PickupType::Gold && type != PickupType::Heart; }
    std::string label() const;
};

Pickup makeGold(Vec2 pos, int value, Vec2 vel);
Pickup makeSpellPickup(Vec2 pos, SpellId spell);
Pickup makeWandPickup(Vec2 pos, Wand wand);
Pickup makePotion(Vec2 pos, MatId mat, int amount);
Pickup makeChest(Vec2 pos);
Pickup makeHeart(Vec2 pos);
Pickup makePerkAltar(Vec2 pos, PerkId perk, int group);

/// Random potion contents for a depth tier.
MatId randomPotionMaterial(int tier, Rng& rng);

} // namespace pm
