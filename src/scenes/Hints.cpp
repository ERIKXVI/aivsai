#include "scenes/Hints.hpp"

#include <algorithm>
#include <iterator>
#include <utility>
#include <vector>

#include "game/Level.hpp"
#include "game/Run.hpp"
#include "game/Wand.hpp"
#include "render/ColorUtil.hpp"
#include "ui/Ui.hpp"

namespace pm {

bool guideCandidate(SpellId s) {
    if (s == SpellId::None) return false;
    return spellDef(s).type != SpellType::Projectile || safeToAutoSlot(s);
}

WandGuide pickWandGuide(const RunState& run) {
    WandGuide guide;
    const auto from = std::find_if(run.inventory.begin(), run.inventory.end(), guideCandidate);
    if (from != run.inventory.end()) {
        guide.from = static_cast<int>(from - run.inventory.begin());
        guide.booster = spellDef(*from).type != SpellType::Projectile;
    }
    std::vector<std::pair<float, int>> byDamage; // (first-cast damage, wand), hardest first
    for (size_t wi = 0; wi < run.wands.size(); ++wi) {
        int pos = 0;
        const Wand& w = run.wands[wi];
        float damage = 0.0f;
        for (const ProjectileLaunch& l : buildShot(w.slots, w.order, pos, w.spellsPerCast).projectiles) damage += l.def.damage;
        byDamage.emplace_back(-damage, static_cast<int>(wi));
    }
    std::stable_sort(byDamage.begin(), byDamage.end());
    const auto isAttack = [](SpellId s) { return s != SpellId::None && spellDef(s).type == SpellType::Projectile; };
    for (const auto& entry : byDamage) {
        const int wi = entry.second;
        const auto& slots = run.wands[static_cast<size_t>(wi)].slots;
        const auto empty = std::find(slots.begin(), slots.end(), SpellId::None);
        if (empty == slots.end()) continue;
        auto to = empty;
        if (guide.booster) {
            // Onto the first attack spell that has room behind it to move along; a shuffle
            // wand has no "after it" to boost.
            if (run.wands[static_cast<size_t>(wi)].shuffle) continue;
            to = std::find_if(slots.begin(), slots.end(), isAttack);
            if (to == slots.end() || std::find(to + 1, slots.end(), SpellId::None) == slots.end()) continue;
        }
        guide.wand = wi;
        guide.slot = static_cast<int>(to - slots.begin());
        break;
    }
    return guide;
}

namespace {

constexpr float kShowTime = 7.0f;

struct HintText {
    const char* keyboard;
    const char* gamepad;
};

constexpr HintText kTexts[] = {
    {"Hold Q to draw a liquid or powder into your flask (one kind at a time) - right-click pours it out",
     "Hold Y to draw a liquid or powder into your flask (one kind at a time) - LT pours it out"},
    {"That spell went to your satchel - press Tab and drag it onto a wand",
     "That spell went to your satchel - press Back and place it on a wand"},
    {"Wounded? Pink Vitae heals - absorb it with Q, then drink with F",
     "Wounded? Pink Vitae heals - absorb it with Y, then drink with X"},
    {"A sanctum: buy spells with E, then claim one perk at the altars",
     "A sanctum: buy spells with B, then claim one perk at the altars"},
    {"A transmutation circle - hold right-click to pour the matter it craves into its basin and earn a perk",
     "A transmutation circle - hold LT to pour the matter it craves into its basin and earn a perk"},
    {"The violet arrow beside you leads along the way to the portal - press M to see it on the map",
     "The violet arrow beside you leads along the way to the portal - D-pad down shows the map"},
    {"Gold ore crumbles into gold dust when blasted - walk over the dust to collect it",
     "Gold ore crumbles into gold dust when blasted - walk over the dust to collect it"},
    {"You are burning! Drink water from your flask with F, or dive into water",
     "You are burning! Drink water from your flask with X, or dive into water"},
    {"Your satchel holds spells - press Tab to put one on a wand",
     "Your satchel holds spells - press Back to put one on a wand"},
    {"Drag the glowing spell into the glowing slot - the wand casts its spells from left to right",
     "Pick up the glowing spell with A and place it in the glowing slot with A"},
    {"Shoot a lantern down onto your foes - it shatters into burning oil",
     "Shoot a lantern down onto your foes - it shatters into burning oil"},
    {"A guardian's lair: slay its keeper for a choice of perks, a hoard and its essence - or leave it be, the fight is optional",
     "A guardian's lair: slay its keeper for a choice of perks, a hoard and its essence - or leave it be, the fight is optional"},
    {"An elixir! Drink it with F: quicksilver lends haste, ichor lends your spells fury",
     "An elixir! Drink it with X: quicksilver lends haste, ichor lends your spells fury"},
};
static_assert(std::size(kTexts) == static_cast<size_t>(Hint::Count), "a text for every Hint");

/// True when a satchel spell could go onto a wand right now.
bool roomForSatchelSpell(const RunState& run) {
    // Only a spell worth teaching with (not the kit's bomb) earns the nudge, and either kind
    // needs an empty slot: an attack spell goes into it, a booster pushes the deck along into it.
    if (std::none_of(run.inventory.begin(), run.inventory.end(), guideCandidate)) return false;
    return std::any_of(run.wands.begin(), run.wands.end(), [](const Wand& w) {
        return std::find(w.slots.begin(), w.slots.end(), SpellId::None) != w.slots.end();
    });
}

/// True when a hanging lantern near the alchemist has a creature close beneath it.
bool lanternOverFoe(const Level& level) {
    const Vec2 me = level.player().center();
    for (const Lantern& l : level.lanterns()) {
        if (l.fallSpeed > 0.0f || (l.pos - me).lengthSq() > 60.0f * 60.0f) continue;
        for (const auto& e : level.enemies())
            if (e->alive() && !e->disguised() && e->team == Team::Monster && e->body.pos.y > l.pos.y && (e->body.pos - l.pos).lengthSq() < 40.0f * 40.0f)
                return true;
    }
    return false;
}

/// Looks for a cell matching `pred` in a square around `center`, sampling every other cell.
template <typename Pred>
bool nearby(const World& world, Vec2 center, int radius, Pred pred) {
    const int cx = floorToInt(center.x), cy = floorToInt(center.y);
    for (int y = cy - radius; y <= cy + radius; y += 2)
        for (int x = cx - radius; x <= cx + radius; x += 2)
            if (world.inBounds(x, y) && pred(world.at(x, y).mat)) return true;
    return false;
}

} // namespace

void HintTracker::reset() {
    active_ = Hint::Count;
    timer_ = 0.0f;
}

const char* HintTracker::text(Hint h, bool gamepad) {
    const HintText& t = kTexts[static_cast<int>(h)];
    return gamepad ? t.gamepad : t.keyboard;
}

void HintTracker::show(Hint h) {
    active_ = h;
    timer_ = kShowTime;
    seen_ |= 1u << static_cast<unsigned>(h);
}

void HintTracker::update(const Level& level, const RunState& run, float dt) {
    // Burning is urgent enough to replace whatever is showing.
    const Player& player = level.player();
    if (!seen(Hint::Burning) && player.status.burning > 0.0f && player.resist[static_cast<size_t>(DamageType::Fire)] > 0.0f) {
        show(Hint::Burning);
        return;
    }
    if (active_ != Hint::Count) {
        timer_ -= dt;
        if (timer_ <= 0.0f) active_ = Hint::Count;
        return;
    }
    const float t = level.time();
    if (t < 2.0f) return;
    if (level.isSanctum()) {
        if (!seen(Hint::Sanctum)) show(Hint::Sanctum);
        return;
    }
    // The opening controls card covers the first half minute of the first stage.
    if (run.levelIndex == 0 && t < 30.0f) return;

    if (!seen(Hint::Satchel) && level.spellsToSatchel() > 0) {
        show(Hint::Satchel);
        return;
    }
    if (!seen(Hint::EditWands) && run.levelIndex == 0 && t > 32.0f && roomForSatchelSpell(run)) {
        show(Hint::EditWands);
        return;
    }
    if (!seen(Hint::Heal) && player.hp < player.maxHp * 0.4f) {
        show(Hint::Heal);
        return;
    }
    if (!seen(Hint::Transmuter)) {
        for (const Transmuter& tm : level.transmuters()) {
            if (!tm.complete && (tm.pos - player.center()).lengthSq() < 70.0f * 70.0f) {
                show(Hint::Transmuter);
                return;
            }
        }
    }
    if (!seen(Hint::Elixir) && (run.flask.mat == mat::Quicksilver || run.flask.mat == mat::Ichor) && !run.flask.empty()) {
        show(Hint::Elixir);
        return;
    }
    if (!seen(Hint::Compass) && run.levelIndex == 0 && t > 35.0f) {
        show(Hint::Compass);
        return;
    }
    // World scans are throttled.
    scanTimer_ -= dt;
    if (scanTimer_ > 0.0f) return;
    scanTimer_ = 0.5f;
    const World& world = level.world();
    if (!seen(Hint::Absorb) && run.flask.amount < Flask::kCapacity &&
        nearby(world, player.center(), 14, [](MatId m) { return matDef(m).phase == Phase::Liquid; })) {
        show(Hint::Absorb);
        return;
    }
    if (!seen(Hint::Gold) && nearby(world, player.center(), 20, [](MatId m) { return m == mat::GoldOre; })) {
        show(Hint::Gold);
        return;
    }
    if (!seen(Hint::Lair)) {
        // Introduce the lair as its keeper notices the alchemist (it sees far down the
        // passage), or when they come close to it unseen.
        const Enemy* g = level.nearestGuardian(player.center(), 240.0f);
        if (g && (g->aggro() || (g->body.pos - player.center()).lengthSq() < 110.0f * 110.0f)) {
            show(Hint::Lair);
            return;
        }
    }
    if (!seen(Hint::Lantern) && lanternOverFoe(level)) {
        show(Hint::Lantern);
        return;
    }
}

void HintTracker::draw(float screenW, float screenH, bool gamepad) const {
    if (active_ == Hint::Count) return;
    const float s = static_cast<float>(ui::scale());
    const int size = ui::fontSize(ui::scale());
    const char* text = HintTracker::text(active_, gamepad);
    const float a = clamp(std::min((kShowTime - timer_) * 3.0f, timer_ * 1.5f), 0.0f, 1.0f);
    const float w = static_cast<float>(ui::measure(text, size)) + 20.0f * s, hgt = static_cast<float>(size) + 10.0f * s;
    Rectangle r{screenW * 0.5f - w * 0.5f, screenH - 64.0f * s - (1.0f - a) * 6.0f * s, w, hgt};
    DrawRectangleRec(r, Color{14, 10, 22, static_cast<unsigned char>(210.0f * a)});
    DrawRectangleLinesEx(r, 1.0f, withAlpha(ui::color::kGold, static_cast<uint8_t>(150.0f * a)));
    ui::text(text, screenW * 0.5f, r.y + 5.0f * s, size, Color{240, 230, 210, static_cast<unsigned char>(255.0f * a)}, ui::Align::Center);
}

} // namespace pm
