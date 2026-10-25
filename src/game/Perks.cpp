#include "game/Perks.hpp"

namespace pm {

namespace {

// clang-format off
const PerkDef kPerks[kPerkCount] = {
    {"Vitality",          "+40 max health, fully healed.",                        0xff5a6a, true},
    {"Salamander Skin",   "Immune to fire and burning.",                          0xff8a30, false},
    {"Blast Shield",      "Immune to explosions, including your own.",            0xffd060, false},
    {"Toxic Blood",       "Immune to acid, toxins and miasma.",                   0x8aff40, false},
    {"Insulated",         "Immune to electricity.",                               0xc0d8ff, false},
    {"Feather Soul",      "Levitate twice as long.",                              0xe0e8ff, true},
    {"Quicksilver Feet",  "Move 25% faster.",                                     0xb0c0d0, true},
    {"Midas' Greed",      "Gold is worth half again and flies to you from afar.", 0xffd040, false},
    {"Vampirism",         "Heal 4 health for every kill.",                        0xc02040, true},
    {"Glass Cannon",      "Deal double damage. Max health halved.",               0x80e0ff, false},
    {"Arcane Flow",       "Wands recharge mana 60% faster.",                      0x8a70ff, true},
    {"Second Life",       "Return from death once, at half health.",              0xfff0c0, false},
    {"Thorns",            "Creatures that strike you take damage.",               0x60a040, true},
    {"Gills",             "Breathe underwater and swim swiftly.",                 0x40a0ff, false},
    {"Critical Mind",     "+15% critical hit chance on all spells.",              0xff60c0, true},
    {"Expansion",         "+2 spell slots on every wand you hold.",               0x70d0a0, true},
    {"Lucky Day",         "Chests and shops offer better treasures.",             0x50ff90, false},
    {"Phoenix Step",      "You leave flames where you walk. Fire heals you.",     0xff5020, false},
    {"Volatile Foes",     "Slain creatures explode.",                             0xff7040, false},
    {"Homunculus",        "A little homunculus follows you and fights.",          0xe09090, true},
    {"Perpetual Motion",  "Cast delay and recharge reduced by 20%.",              0xa0a0ff, true},
    {"Iron Stomach",      "Drinking any liquid heals you a little.",              0xd0a070, false},
};
// clang-format on

} // namespace

const PerkDef& perkDef(PerkId id) { return kPerks[static_cast<int>(id)]; }

} // namespace pm
