#include "game/Level.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/Profiler.hpp"
#include "core/Timing.hpp"
#include "render/Sprites.hpp"

namespace pm {

namespace {
constexpr float kInteractRange = 16.0f;
constexpr float kEnemyActiveRange = 380.0f;
} // namespace

Level::Level(LevelLayout layout, RunState& run, Audio& audio, ThreadPool& pool)
    : layout_(std::move(layout)), run_(run), audio_(audio), pool_(pool), rng_(layout_.seed ^ 0x9E3779B9u),
      fxRng_(layout_.seed ^ 0x51ED270Bu) {
    player_ = std::make_unique<Player>(layout_.playerStart - Vec2{0.0f, 6.0f}, run_);
    player_->id = nextId();
    populate();
    events_.begin(*this, rng_);
}

Level::~Level() = default;

Affix Level::rollAffix() { return static_cast<Affix>(rng_.rangeInt(1, static_cast<int>(Affix::Count) - 1)); }

void Level::populate() {
    const int tier = std::min(4, layout_.levelIndex);
    const bool lucky = run_.hasPerk(PerkId::LuckyDay);
    int shopIndex = 0;
    int guardians = 0;
    std::vector<PerkId> offered;
    // A guardian's lair: the wand in its hoard is a finer one.
    const Rectf* lair = nullptr;
    for (const SpawnPoint& s : layout_.spawns)
        if (s.kind == SpawnKind::LairGuardian)
            for (const Rectf& v : layout_.vaults)
                if (v.contains(s.pos)) lair = &v;
    for (const SpawnPoint& s : layout_.spawns) {
        switch (s.kind) {
        case SpawnKind::GroundEnemy:
        case SpawnKind::AirEnemy: {
            EnemyType t = pickEnemy(layout_.levelIndex, s.kind == SpawnKind::AirEnemy, s.difficulty, rng_);
            const EnemyDef& d = enemyDef(t);
            Vec2 pos = s.kind == SpawnKind::GroundEnemy ? Vec2{s.pos.x, s.pos.y - d.halfH} : s.pos;
            Enemy& e = spawnEnemy(t, pos);
            float eliteChance = 0.05f + 0.03f * static_cast<float>(layout_.levelIndex) + 0.05f * s.difficulty;
            if (run_.under(Ordeal::GatheringElites)) eliteChance *= 2.0f;
            if (t != EnemyType::Worm && rng_.chance(eliteChance)) e.makeElite(rollAffix());
            break;
        }
        case SpawnKind::Guardian: {
            // The first guardian of a vault is elite; shallow stages only post one.
            if (guardians > 0 && layout_.levelIndex < 2) break;
            EnemyType t = pickEnemy(layout_.levelIndex, false, 1.0f, rng_);
            for (int tries = 0; tries < 6 && t == EnemyType::Worm; ++tries) t = pickEnemy(layout_.levelIndex, false, 1.0f, rng_);
            if (t == EnemyType::Worm) t = EnemyType::Ghoul;
            Enemy& e = spawnEnemy(t, {s.pos.x, s.pos.y - enemyDef(t).halfH});
            if (guardians++ % 2 == 0) e.makeElite(rollAffix());
            break;
        }
        case SpawnKind::LairGuardian: {
            const EnemyType t = guardianFor(layout_.levelIndex);
            const EnemyDef& d = enemyDef(t);
            spawnEnemy(t, d.flying ? s.pos - Vec2{0.0f, 24.0f} : Vec2{s.pos.x, s.pos.y - d.halfH});
            break;
        }
        case SpawnKind::Wand: {
            const bool hoard = lair && lair->contains(s.pos);
            Wand w = makeRandomWand(std::min(4, tier + (hoard ? 2 : 1)), rng_);
            if (hoard) {
                // Beating a keeper always pays a showpiece, set in the hoard wand's first free slot.
                const auto slot = std::find(w.slots.begin(), w.slots.end(), SpellId::None);
                if (slot != w.slots.end()) {
                    *slot = randomRareSpell(tier, rng_);
                    w.resetDeck();
                }
            }
            pickups_.push_back(makeWandPickup(s.pos - Vec2{0.0f, 6.0f}, std::move(w)));
            break;
        }
        case SpawnKind::Chest: {
            // Some chests along the route are mimics, more of them deeper down; a vault's hoard
            // is always what it seems.
            const bool inVault = std::any_of(layout_.vaults.begin(), layout_.vaults.end(), [&](const Rectf& v) { return v.contains(s.pos); });
            if (!isSanctum() && !inVault && rng_.chance(layout_.levelIndex == 0 ? 0.08f : 0.16f))
                spawnEnemy(EnemyType::Mimic, {s.pos.x, s.pos.y - enemyDef(EnemyType::Mimic).halfH});
            else
                pickups_.push_back(makeChest(s.pos - Vec2{0.0f, 4.0f}));
            break;
        }
        case SpawnKind::Potion:
            pickups_.push_back(makePotion(s.pos - Vec2{0.0f, 5.0f}, randomPotionMaterial(tier, rng_), 700));
            break;
        case SpawnKind::Spell:
            if (isSanctum()) {
                // Shop: three spells and one wand from the coming depth (a sanctum shares the index
                // of the stage just cleared), priced for it.
                const int shopTier = std::min(4, tier + 1 + (lucky ? 1 : 0));
                const float priceMul = (1.0f + 0.2f * static_cast<float>(layout_.levelIndex)) * (run_.under(Ordeal::DearWares) ? 1.5f : 1.0f);
                Pickup p;
                if (shopIndex == 3) {
                    Wand w = makeRandomWand(shopTier, rng_);
                    p = makeWandPickup(s.pos - Vec2{0.0f, 12.0f}, std::move(w));
                    p.price = static_cast<int>((180.0f + 70.0f * static_cast<float>(shopTier)) * priceMul);
                } else {
                    SpellId spell = randomSpell(shopTier, rng_);
                    p = makeSpellPickup(s.pos - Vec2{0.0f, 12.0f}, spell);
                    p.price = static_cast<int>(static_cast<float>(spellDef(spell).price) * priceMul);
                }
                p.body.gravityScale = 0.0f;
                pickups_.push_back(std::move(p));
                ++shopIndex;
            } else {
                pickups_.push_back(makeSpellPickup(s.pos - Vec2{0.0f, 5.0f}, randomSpell(tier, rng_)));
            }
            break;
        case SpawnKind::RareSpell: {
            Pickup p = makeSpellPickup(s.pos - Vec2{0.0f, 5.0f}, randomRareSpell(tier, rng_));
            p.rare = true;
            pickups_.push_back(std::move(p));
            break;
        }
        case SpawnKind::Lantern: {
            // Hang from the first solid above the spawn point, climbing through steam or smoke
            // pooled under the ceiling (updateLanterns drops a lantern whose anchor, 5 rows
            // above its centre, cannot bear it). No ceiling in reach: no lantern.
            Vec2 hook = s.pos;
            const int hx = floorToInt(hook.x);
            auto ceiling = [&] {
                const int y = floorToInt(hook.y) - 1;
                return !world().inBounds(hx, y) || phaseOf(world().at(hx, y).mat) == Phase::Solid;
            };
            for (int k = 0; k < 40 && !ceiling(); ++k) hook.y -= 1.0f;
            if (!world().inBounds(hx, floorToInt(hook.y) - 1) || !ceiling()) break;
            lanterns_.push_back({hook + Vec2{0.0f, 4.0f}, rng_.range(0.0f, kTau)});
            lanterns_.back().id = static_cast<uint32_t>(lanterns_.size()); // shattered ones are erased later
            break;
        }
        case SpawnKind::Transmuter: {
            // Each stage's basin craves a material found nearby; the first teaches with water.
            static constexpr MatId kWants[kStageCount][3] = {{mat::Water, mat::Oil, mat::Blood},
                                                              {mat::Water, mat::Brine, mat::Blood},
                                                              {mat::Acid, mat::Toxic, mat::Water},
                                                              {mat::Lava, mat::Oil, mat::Blood},
                                                              {mat::Lava, mat::Quicksilver, mat::Blood}};
            const int stage = clamp(layout_.levelIndex, 0, kFinalStage);
            Transmuter t;
            t.pos = s.pos;
            t.wants = stage == 0 ? static_cast<MatId>(mat::Water) : rng_.pick(kWants[stage]);
            transmuters_.push_back(t);
            break;
        }
        case SpawnKind::Altar: {
            PerkId perk = rollPerk(offered);
            offered.push_back(perk);
            pickups_.push_back(makePerkAltar(s.pos - Vec2{0.0f, 5.0f}, perk, 0));
            break;
        }
        }
    }
    // Companions granted by perks join at the entrance.
    for (int i = 0; i < run_.perkCount(PerkId::Companion); ++i)
        spawnEnemy(EnemyType::Companion, player_->body.pos + Vec2{static_cast<float>(-6 - 4 * i), 0.0f});
    // The final stage guards the exit with the boss.
    if (layout_.biome == BiomeId::Athanor) spawnEnemy(EnemyType::Boss, layout_.exitPos - Vec2{0.0f, 60.0f});
}

Enemy& Level::spawnEnemy(EnemyType type, Vec2 pos) {
    // Foes harden with depth and with the difficulty. The final boss's health is set for its
    // stage already, so it takes only the difficulty; the alchemist's homunculus takes neither.
    const float difficulty = run_.difficulty == Difficulty::Apprentice ? 0.8f : run_.difficulty == Difficulty::Archmage ? 1.3f : 1.0f;
    float scale = (type == EnemyType::Boss ? 1.0f : 1.0f + 0.32f * static_cast<float>(layout_.levelIndex)) * difficulty;
    if (type == EnemyType::Companion) scale = 1.0f;
    // Ordeals harden every foe (the alchemist's homunculus aside), and the keepers most of all.
    const bool foe = type != EnemyType::Companion;
    if (foe && run_.under(Ordeal::HardenedFoes)) scale *= 1.25f;
    if ((enemyDef(type).guardian || type == EnemyType::Boss) && run_.under(Ordeal::WrathfulKeepers)) scale *= 1.4f;
    enemies_.push_back(std::make_unique<Enemy>(type, pos, nextId(), scale));
    Enemy& e = *enemies_.back();
    if (type == EnemyType::Companion) e.team = Team::Player;
    if (foe && run_.under(Ordeal::CruelBlows)) e.setStrikeMul(1.25f);
    return e;
}

LevelEvent Level::pollEvent() {
    LevelEvent e = event_;
    event_ = LevelEvent::None;
    return e;
}

const char* Level::actorName(uint32_t id) const {
    for (const auto& e : enemies_)
        if (e->id == id) return e->def().name;
    return nullptr;
}

const Enemy* Level::boss() const {
    for (const auto& e : enemies_)
        if (e->type() == EnemyType::Boss && e->alive()) return e.get();
    return nullptr;
}

const Enemy* Level::nearestGuardian(Vec2 pos, float radius) const {
    const Enemy* best = nullptr;
    float bestSq = radius * radius;
    for (const auto& e : enemies_) {
        if (!e->guardian() || !e->alive()) continue;
        const float d = (e->body.pos - pos).lengthSq();
        if (d < bestSq) {
            bestSq = d;
            best = e.get();
        }
    }
    return best;
}

const Enemy* Level::bossBarTarget() const {
    if (const Enemy* b = boss(); b && b->aggro()) return b;
    const Enemy* g = nearestGuardian(player_->body.pos, 240.0f);
    return g && g->aggro() ? g : nullptr;
}

void Level::tick(const PlayerInput& input) {
    ScopedTimer tickTimer(ProfileSection::Tick);
    const float dt = kTickDt;
    heldSinceTick_ = false;
    input_ = input;
    ++ticks_;
    time_ += dt;
    if (player_->alive()) run_.stats.time += dt;

    updateEntities(input, dt);

    {
        ScopedTimer simTimer(ProfileSection::Simulation);
        world().step(pool_);
    }
    handleSimEvents();
    {
        ScopedTimer particleTimer(ProfileSection::Particles);
        particles_.update(world(), dt);
        spawnAmbience();
    }
    updatePickups(dt);
    if (input.interactPressed && player_->alive()) interact();
    updateEffects(dt);
    events_.tick(*this);
    updateLanterns(dt);
    updateTrees();

    enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(), [](const auto& e) { return e->dead; }),
                   enemies_.end());

    // The exit portal draws the player in once no guardian remains.
    if (player_->alive() && !boss()) {
        Vec2 portal = layout_.exitPos - Vec2{0.0f, 8.0f};
        Vec2 to = portal - player_->body.pos;
        if (to.length() < 12.0f) {
            exitHold_ += dt;
            player_->body.vel = player_->body.vel * 0.8f + to * 2.0f;
            if (exitHold_ > 0.5f && !exitTriggered_) {
                exitTriggered_ = true;
                event_ = LevelEvent::ExitReached;
                audio_.play(Sfx::Portal, 0.8f);
            }
        } else {
            exitHold_ = 0.0f;
        }
    }

    updateIntensity();
    if (ticks_ % 10 == 0) updateTransmuters();
    if (ticks_ % 30 == 0 && player_->alive())
        for (const Rectf& v : layout_.vaults)
            if (v.contains(player_->body.pos)) earnFeat(FeatId::VaultRaider);
    audio_.setListener(player_->body.pos);
}

void Level::updateEntities(const PlayerInput& input, float dt) {
    ScopedTimer timer(ProfileSection::Entities);
    player_->setInput(input);
    if (player_->alive()) player_->tick(*this);
    else player_->body.prevPos = player_->body.pos;

    // Index loops: creatures and projectiles may spawn more of themselves while updating.
    const Vec2 playerPos = player_->body.pos;
    for (size_t i = 0; i < enemies_.size(); ++i) {
        Enemy& e = *enemies_[i];
        if (!e.alive()) continue;
        const bool far = (e.body.pos - playerPos).lengthSq() > kEnemyActiveRange * kEnemyActiveRange;
        if (far && !e.aggro() && e.type() != EnemyType::Companion) {
            e.body.prevPos = e.body.pos; // dormant until the player comes near
            continue;
        }
        e.tick(*this);
    }

    for (size_t i = 0; i < projectiles_.size(); ++i) {
        if (!projectiles_[i].dead) updateProjectile(*this, projectiles_[i], dt);
    }
    projectiles_.erase(std::remove_if(projectiles_.begin(), projectiles_.end(), [](const Projectile& p) { return p.dead; }),
                       projectiles_.end());
    for (Projectile& p : pendingProjectiles_) projectiles_.push_back(std::move(p));
    pendingProjectiles_.clear();
}

void Level::handleSimEvents() {
    if (world().events().empty()) return;
    // Burning gunpowder reports one event per grain; the world merges them into blasts.
    for (const SimBlast& b : world().takeBlasts()) {
        ExplosionSpec spec;
        spec.radius = b.radius();
        spec.damage = std::min(110.0f, 12.0f + static_cast<float>(b.grains) * 1.2f);
        spec.power = 150;
        spec.incendiary = true;
        spec.team = Team::Monster;
        spec.hurtsOwner = true;
        spec.cause = "a gunpowder blast";
        explode(b.center, spec);
    }
}

void Level::updatePickups(float dt) {
    Player& pl = *player_;
    const bool greedy = run_.hasPerk(PerkId::MidasGreed);
    for (Pickup& p : pickups_) {
        if (p.dead) continue;
        p.age += dt;
        p.body.prevPos = p.body.pos;
        if (p.body.gravityScale > 0.0f) {
            moveBody(p.body, world(), dt);
            unbury(p.body, world());
        }
        if (!pl.alive()) continue;
        Vec2 to = pl.body.pos - p.body.pos;
        float dist = to.length();
        if (p.type == PickupType::Gold) {
            // Coins close by drift to the alchemist; Midas' Greed pulls from much further.
            const float reach = greedy ? 70.0f : 22.0f, pull = greedy ? 900.0f : 450.0f;
            if (dist < reach && p.age > (greedy ? 0.3f : 0.4f)) p.body.vel += to.normalized() * (pull * dt);
            if (dist < 7.0f && p.age > 0.25f) {
                int value = greedy ? p.amount * 3 / 2 : p.amount;
                run_.gold += value;
                run_.stats.goldCollected += value;
                p.dead = true;
                audio_.play(Sfx::Coin, 0.45f, rng_.range(0.95f, 1.1f));
                addFloatText(p.body.pos, "+" + std::to_string(value), 0xffd040, 0.8f);
            }
        } else if (p.type == PickupType::Heart && dist < 8.0f && p.age > 0.25f) {
            pl.hp = std::min(pl.maxHp, pl.hp + static_cast<float>(p.amount));
            run_.hp = pl.hp;
            p.dead = true;
            audio_.play(Sfx::Pickup, 0.6f);
            addFloatText(p.body.pos, "+" + std::to_string(p.amount), 0xff6080);
        }
    }
    pickups_.erase(std::remove_if(pickups_.begin(), pickups_.end(), [](const Pickup& p) { return p.dead; }),
                   pickups_.end());
}

const Pickup* Level::focusedPickup() const {
    const Pickup* best = nullptr;
    float bestDist = kInteractRange;
    for (const Pickup& p : pickups_) {
        if (p.dead || !p.interactive() || (p.type == PickupType::Chest && p.opened)) continue;
        float d = (p.body.pos - player_->body.pos).length();
        if (d < bestDist) {
            bestDist = d;
            best = &p;
        }
    }
    return best;
}

void Level::interact() {
    const Pickup* focused = focusedPickup();
    if (!focused) return;
    Pickup& p = pickups_[static_cast<size_t>(focused - pickups_.data())];
    Player& pl = *player_;
    // Note: spawning new pickups reallocates `pickups_`, so `p` must not be used after that.
    const Vec2 at = p.body.pos;

    if (p.price > 0 && run_.gold < p.price) {
        audio_.play(Sfx::UiDeny, 0.6f);
        addFloatText(p.body.pos - Vec2{0.0f, 10.0f}, "Not enough gold", 0xff8060);
        return;
    }
    auto pay = [&] {
        if (p.price > 0) {
            run_.gold -= p.price;
            audio_.play(Sfx::Coin, 0.6f, 0.8f);
        }
    };

    switch (p.type) {
    case PickupType::Spell: {
        const bool slotted = run_.trySlotIntoActiveWand(p.spell);
        if (!slotted && !run_.addSpell(p.spell)) {
            audio_.play(Sfx::UiDeny, 0.6f);
            addFloatText(p.body.pos - Vec2{0.0f, 10.0f}, "Satchel full", 0xff8060);
            return;
        }
        pay();
        p.dead = true;
        audio_.play(Sfx::Pickup, 0.7f);
        if (p.rare) {
            addFloatText(p.body.pos - Vec2{0.0f, 18.0f}, "A rare find!", 0xffd27a, 1.1f);
            addFlash(p.body.pos, 0xffe0a0, 1.2f, 40.0f, 0.25f);
        }
        addFloatText(p.body.pos - Vec2{0.0f, 10.0f}, spellDef(p.spell).name, 0xc0e0ff);
        if (slotted) {
            addFloatText(p.body.pos - Vec2{0.0f, 4.0f}, "into " + run_.currentWand()->name, 0x9fb4d0, 0.6f);
            earnFeat(FeatId::Spellwright);
        } else {
            addFloatText(p.body.pos - Vec2{0.0f, 4.0f}, "to the satchel", 0x9fb4d0, 0.6f);
            ++spellsToSatchel_;
            lastToSatchel_ = p.spell;
        }
        break;
    }
    case PickupType::Wand: {
        pay();
        Wand w = std::move(*p.wand);
        run_.noteWand(w);
        p.dead = true;
        if (static_cast<int>(run_.wands.size()) < RunState::kMaxWands) {
            run_.wands.push_back(std::move(w));
            run_.activeWand = static_cast<int>(run_.wands.size()) - 1;
        } else {
            Wand old = std::move(*run_.currentWand());
            *run_.currentWand() = std::move(w);
            pickups_.push_back(makeWandPickup(pl.body.pos, std::move(old)));
        }
        audio_.play(Sfx::Pickup, 0.8f, 0.8f);
        addFloatText(at - Vec2{0.0f, 8.0f}, run_.currentWand()->name, 0xffe0a0);
        break;
    }
    case PickupType::Potion: {
        pay();
        Flask& f = run_.flask;
        if (f.empty() || f.mat == p.mat) {
            f.mat = p.mat;
            f.amount = std::min(Flask::kCapacity, f.amount + p.amount);
            p.dead = true;
        } else {
            std::swap(f.mat, p.mat);
            std::swap(f.amount, p.amount);
        }
        audio_.play(Sfx::Potion, 0.7f);
        addFloatText(p.body.pos - Vec2{0.0f, 8.0f}, matDef(f.mat).name, 0xa0d0ff);
        break;
    }
    case PickupType::Chest:
        p.opened = true;
        run_.stats.chestsOpened++;
        audio_.play(Sfx::ChestOpen, 0.8f);
        dropLoot(at - Vec2{0.0f, 4.0f}, std::min(4, layout_.levelIndex), true);
        addFlash(at, 0xffd070, 2.0f, 60.0f, 0.5f);
        break;
    case PickupType::PerkAltar: {
        const PerkId perk = p.perk;
        const int group = p.altarGroup;
        for (Pickup& other : pickups_)
            if (other.type == PickupType::PerkAltar && other.altarGroup == group) other.dead = true;
        grantPerk(perk, nullptr);
        break;
    }
    default: break;
    }
}

void Level::updateEffects(float dt) {
    for (Shockwave& w : shockwaves_) w.age += dt;
    shockwaves_.erase(std::remove_if(shockwaves_.begin(), shockwaves_.end(), [](const Shockwave& w) { return w.age >= w.duration; }),
                      shockwaves_.end());
    for (LightFlash& f : flashes_) f.life -= dt;
    flashes_.erase(std::remove_if(flashes_.begin(), flashes_.end(), [](const LightFlash& f) { return f.life <= 0.0f; }),
                   flashes_.end());
    for (FloatText& t : floatTexts_) {
        t.life -= dt;
        t.pos.y -= 18.0f * dt;
        t.pop = std::max(0.0f, t.pop - dt / 0.12f);
    }
    floatTexts_.erase(std::remove_if(floatTexts_.begin(), floatTexts_.end(), [](const FloatText& t) { return t.life <= 0.0f; }),
                      floatTexts_.end());
    shake_ = std::max(0.0f, shake_ - dt * 1.6f);
    hurtPulse_ = std::max(0.0f, hurtPulse_ - dt * 2.5f);
    if (player_->hurtFlash > 0.1f) hurtPulse_ = 1.0f;
}

void Level::updateIntensity() {
    int engaged = 0;
    for (const auto& e : enemies_) {
        if (e->alive() && e->aggro() && e->team == Team::Monster &&
            (e->body.pos - player_->body.pos).lengthSq() < 220.0f * 220.0f)
            ++engaged;
    }
    float target = std::min(1.0f, static_cast<float>(engaged) / 4.0f);
    if (boss()) target = 1.0f;
    intensity_ += (target - intensity_) * 0.01f;
}

void Level::addFlash(Vec2 pos, uint32_t color, float intensity, float radius, float duration) {
    if (flashes_.size() > 48) return;
    flashes_.push_back({pos, ((color >> 16) & 0xFF) / 255.0f * intensity, ((color >> 8) & 0xFF) / 255.0f * intensity,
                        (color & 0xFF) / 255.0f * intensity, radius, duration, duration});
}

void Level::addFloatText(Vec2 pos, const std::string& text, uint32_t color, float scale) {
    if (floatTexts_.size() > 60) floatTexts_.erase(floatTexts_.begin());
    floatTexts_.push_back({pos, text, color, 1.1f, scale});
}

void Level::critPunch(Vec2 pos) {
    constexpr uint32_t kSpacing = 12; // ticks between freezes
    if (ticks_ - lastCritPunch_ >= kSpacing || lastCritPunch_ == 0) {
        addHitStop(1);
        lastCritPunch_ = ticks_;
    }
    addFlash(pos, 0xfff0b0, 1.0f, 18.0f, 0.12f);
    audio_.playAt(Sfx::Hit, pos, 0.5f, 1.6f);
}

void Level::addDamageNumber(uint32_t owner, Vec2 pos, float amount, bool crit) {
    constexpr float kLife = 1.1f, kMergeWindow = 0.4f;
    auto label = [](const FloatText& t) { return std::to_string(static_cast<int>(std::ceil(t.value))) + (t.crit ? "!" : ""); };
    for (FloatText& t : floatTexts_) {
        if (t.owner != owner || t.life < kLife - kMergeWindow) continue;
        t.value += amount;
        t.crit = t.crit || crit;
        t.text = label(t);
        t.color = t.crit ? 0xffe040 : 0xffffff;
        t.scale = t.crit ? 1.3f : 1.0f;
        t.life = kLife;
        t.pop = 1.0f;
        t.pos = pos; // back over the creature: a long fight must not float its tally away
        return;
    }
    addFloatText(pos, "", crit ? 0xffe040 : 0xffffff, crit ? 1.3f : 1.0f);
    FloatText& t = floatTexts_.back();
    t.owner = owner;
    t.value = amount;
    t.crit = crit;
    t.text = label(t);
    t.pop = 1.0f;
}

PerkId Level::rollPerk(const std::vector<PerkId>& exclude) {
    PerkId perk = PerkId::Vitality;
    for (int guard = 0; guard < 60; ++guard) {
        perk = static_cast<PerkId>(rng_.rangeInt(0, kPerkCount - 1));
        const bool owned = !perkDef(perk).stackable && run_.hasPerk(perk);
        const bool taken = std::find(exclude.begin(), exclude.end(), perk) != exclude.end();
        if (!owned && !taken) break;
    }
    return perk;
}

void Level::grantPerk(PerkId perk, const char* reason) {
    run_.hp = player_->hp;
    run_.applyPerk(perk);
    const PerkDef& def = perkDef(perk);
    // Re-derive the player's stats (resistances, health) from the run; moment-to-moment state
    // (levitation, status effects, breath, i-frames) carries on.
    player_->hp = run_.hp;
    player_->refreshFromRun();
    const Vec2 pos = player_->body.pos;
    audio_.play(Sfx::Perk, 0.9f);
    if (reason) addFloatText(pos - Vec2{0.0f, 24.0f}, reason, 0xe0d0ff, 1.1f);
    addFloatText(pos - Vec2{0.0f, 14.0f}, def.name, def.color, 1.4f);
    addFlash(pos, def.color, 3.0f, 120.0f, 0.8f);
    particles_.burst(pos, 60, 160.0f, {255, 240, 200, 255}, {255, 200, 120, 255}, 0.8f, rng_, 0.0f);
}

void Level::updateTransmuters() {
    World& w = world();
    for (Transmuter& t : transmuters_) {
        if (t.complete) continue;
        const int cx = floorToInt(t.pos.x), floorY = floorToInt(t.pos.y);
        // Only matter the alchemist brought counts: a circle beside a natural pool must
        // not fill itself.
        int count = 0;
        for (int y = floorY - 6; y <= floorY; ++y)
            for (int x = cx - 9; x <= cx + 9; ++x)
                if (w.inBounds(x, y) && w.at(x, y).mat == t.wants && (w.at(x, y).flags & cellflag::kPlayerMade)) ++count;
        t.progress = count;
        if (count < t.required) continue;
        // The offering is accepted: the matter vanishes in a burst of light.
        for (int y = floorY - 8; y <= floorY; ++y)
            for (int x = cx - 9; x <= cx + 9; ++x)
                if (w.inBounds(x, y) && w.at(x, y).mat == t.wants) w.clear(x, y);
        t.complete = true;
        earnFeat(FeatId::Transmutation);
        t.progress = t.required;
        Rgba col = materialColor(t.wants);
        col.a = 255;
        particles_.burst(t.pos - Vec2{0.0f, 4.0f}, 80, 180.0f, col, {255, 230, 180, 255}, 1.0f, rng_, -0.1f);
        addShake(0.3f);
        audio_.play(Sfx::Portal, 0.6f, 1.6f);
        if (player_->alive() && (player_->body.pos - t.pos).length() < 400.0f) grantPerk(rollPerk({}), "Transmutation!");
    }
}

void Level::debugTeleport(Vec2 pos) {
    player_->body.pos = player_->body.prevPos = pos;
    player_->body.vel = {};
    unbury(player_->body, world());
}

void Level::debugWarpAhead() {
    const auto& path = layout_.mainPath;
    if (path.empty()) return;
    size_t nearest = 0;
    float best = 1e30f;
    for (size_t i = 0; i < path.size(); ++i) {
        float d = (path[i] - player_->body.pos).lengthSq();
        if (d < best) {
            best = d;
            nearest = i;
        }
    }
    // Always advance past the previous warp so repeated warps make progress.
    debugWarpIndex_ = std::max(debugWarpIndex_ + 1, nearest + 1);
    const Vec2 target = debugWarpIndex_ < path.size() ? path[debugWarpIndex_] : layout_.exitPos - Vec2{0.0f, 10.0f};
    std::printf("[autoplay] warp to waypoint %zu\n", debugWarpIndex_);
    std::fflush(stdout);
    player_->body.pos = player_->body.prevPos = target;
    player_->body.vel = {};
    dig(target, 9.0f, 254, false);
    unbury(player_->body, world());
}

void Level::onPlayerDeath() {
    if (playerDeathHandled_) return;
    playerDeathHandled_ = true;
    run_.hp = 0.0f;
    audio_.play(Sfx::PlayerDie, 0.9f);
    bleed(player_->body.pos, mat::Blood, 60, {0.0f, -80.0f});
    particles_.burst(player_->body.pos, 40, 140.0f, {180, 140, 255, 255}, {120, 80, 255, 255}, 1.0f, rng_, 0.2f);
    // The alchemist's matter comes apart like any creature's, rising as it goes.
    const SpriteFrame& frame = spriteFrame(SpriteId::PlayerIdle, 0);
    const Body& b = player_->body;
    const Vec2 topLeft{b.pos.x - static_cast<float>(frame.w) * 0.5f, b.pos.y + b.halfH - static_cast<float>(frame.h)};
    shatterPixels(frame, topLeft, 1.0f, !player_->facingRight(), b.pos, Rgba{255, 255, 255, 255}, Vec2{0.0f, -1.0f}, 400);
    addShake(0.7f);
    event_ = LevelEvent::PlayerDied;
}

} // namespace pm
