#include "game/Feats.hpp"

#include <array>
#include <cstddef>

namespace pm {

namespace {

constexpr std::array<FeatDef, kFeatCount> kFeats = {{
    {"First Blood", "Slay your first creature."},
    {"Spellwright", "Put a found spell onto a wand."},
    {"Brimming", "Fill your flask to the brim."},
    {"Vitae", "Heal yourself with a draught of Vitae."},
    {"Doused", "Put out your own flames with water."},
    {"Treasure Hunter", "Open five chests in a single run."},
    {"Lanternfall", "Shoot a lantern down from its hook."},
    {"Transmutation", "Complete a transmutation circle."},
    {"Vault Raider", "Set foot inside a vault."},
    {"Elite Hunter", "Slay an elite creature."},
    {"Carnage", "Slay three creatures within a second."},
    {"Toadslayer", "Slay the Black Toad, Keeper of the Mire."},
    {"Wraithbane", "Slay the White Wraith, Keeper of the Frozen Hall."},
    {"Regicide", "Slay the Mine King, Keeper of the Deep Seams."},
    {"Colossus Felled", "Slay the Red Colossus, Keeper of the Forge."},
    {"Deep Delver", "Reach Rubedo, the fourth stage."},
    {"Untouched", "Clear a stage without taking any damage."},
    {"Midas", "Hold 500 gold at once."},
    {"Magnum Opus", "Complete the Great Work."},
    {"Archmage", "Complete the Great Work on Archmage."},
    {"Devotion", "Finish a Daily Rite."},
    {"Pathfinder", "Unlock a new starting path."},
    {"Polymath", "Discover 25 spells."},
    {"Not a Chest", "Slay a mimic."},
}};

} // namespace

const FeatDef& featDef(FeatId id) { return kFeats[static_cast<size_t>(id)]; }

} // namespace pm
