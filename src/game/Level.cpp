#include "game/Level.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "core/Timing.hpp"

namespace pm {

namespace {
constexpr float kInteractRange = 16.0f;
constexpr float kEnemyActiveRange = 380.0f;
} // namespace

Level::Level(LevelLayout layout, RunState& run, Audio& audio, ThreadPool& pool)
    : layout_(std::move(layout)), run_(run), audio_(audio), pool_(pool), rng_(layout_.seed ^ 0x9E3779B9u) {
    player_ = std::make_unique<Player>(layout_.playerStart - Vec2{0.0f, 6.0f}, run_);
    player_->id = nextId();
    populate();
}

Level::~Level() = default;

void Level::populate() {
    const int tier = std::min(4, layout_.levelIndex);
    const bool lucky = run_.hasPerk(PerkId::LuckyDay);
    int altarIndex = 0;
    int shopIndex = 0;
    std::vector<PerkId> offered;
    for (const SpawnPoint& s : layout_.spawns) {
        switch (s.kind) {
        case SpawnKind::GroundEnemy:
        case SpawnKind::AirEnemy: {
            EnemyType t = pickEnemy(layout_.levelIndex, s.kind == SpawnKind::AirEnemy, s.difficulty, rng_);
            const EnemyDef& d = enemyDef(t);
            Vec2 pos = s.kind == SpawnKind::GroundEnemy ? Vec2{s.pos.x, s.pos.y - d.halfH} : s.pos;
            spawnEnemy(t, pos);
            break;
        }
        case SpawnKind::Chest: pickups_.push_back(makeChest(s.pos - Vec2{0.0f, 4.0f})); break;
        case SpawnKind::Potion:
            pickups_.push_back(makePotion(s.pos - Vec2{0.0f, 5.0f}, randomPotionMaterial(tier, rng_), 700));
            break;
        case SpawnKind::Spell:
            if (isSanctum()) {
                // Shop: three spells and one wand, priced for the coming depth.
                const int shopTier = std::min(4, tier + (lucky ? 1 : 0));
                const float priceMul = 1.0f + 0.2f * static_cast<float>(layout_.levelIndex);
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
        case SpawnKind::Lantern: lanterns_.push_back({s.pos + Vec2{0.0f, 4.0f}, rng_.range(0.0f, kTau)}); break;
        case SpawnKind::Altar: {
            PerkId perk;
            int guard = 0;
            do {
                perk = static_cast<PerkId>(rng_.rangeInt(0, kPerkCount - 1));
            } while (guard++ < 50 && ((!perkDef(perk).stackable && run_.hasPerk(perk)) ||
                                      std::find(offered.begin(), offered.end(), perk) != offered.end()));
            offered.push_back(perk);
            pickups_.push_back(makePerkAltar(s.pos - Vec2{0.0f, 5.0f}, perk, 0));
            ++altarIndex;
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
    float scale = 1.0f + 0.32f * static_cast<float>(layout_.levelIndex);
    if (run_.difficulty == Difficulty::Apprentice) scale *= 0.8f;
    if (run_.difficulty == Difficulty::Archmage) scale *= 1.3f;
    if (type == EnemyType::Companion || type == EnemyType::Boss) scale = 1.0f;
    enemies_.push_back(std::make_unique<Enemy>(type, pos, nextId(), scale));
    Enemy& e = *enemies_.back();
    if (type == EnemyType::Companion) e.team = Team::Player;
    return e;
}

LevelEvent Level::pollEvent() {
    LevelEvent e = event_;
    event_ = LevelEvent::None;
    return e;
}

const Enemy* Level::boss() const {
    for (const auto& e : enemies_)
        if (e->type() == EnemyType::Boss && e->alive()) return e.get();
    return nullptr;
}

void Level::tick(const PlayerInput& input) {
    const float dt = kTickDt;
    input_ = input;
    ++ticks_;
    time_ += dt;
    run_.stats.time += dt;

    player_->setInput(input);
    if (player_->alive()) player_->tick(*this);
    else player_->body.prevPos = player_->body.pos;

    const Vec2 playerPos = player_->body.pos;
    for (auto& e : enemies_) {
        if (!e->alive()) continue;
        if ((e->body.pos - playerPos).lengthSq() > kEnemyActiveRange * kEnemyActiveRange && !e->aggro() &&
            e->type() != EnemyType::Companion) {
            e->body.prevPos = e->body.pos;
            continue;
        }
        e->tick(*this);
    }

    for (size_t i = 0; i < projectiles_.size(); ++i) {
        if (!projectiles_[i].dead) updateProjectile(*this, projectiles_[i], dt);
    }
    projectiles_.erase(std::remove_if(projectiles_.begin(), projectiles_.end(), [](const Projectile& p) { return p.dead; }),
                       projectiles_.end());
    for (Projectile& p : pendingProjectiles_) projectiles_.push_back(std::move(p));
    pendingProjectiles_.clear();

    world().step(pool_);
    handleSimEvents();
    particles_.update(world(), dt);
    updatePickups(dt);
    if (input.interactPressed && player_->alive()) interact();
    updateEffects(dt);

    enemies_.erase(std::remove_if(enemies_.begin(), enemies_.end(), [](const auto& e) { return e->dead; }),
                   enemies_.end());

    // The exit portal draws the player in once no guardian remains.
    if (player_->alive() && !boss()) {
        Vec2 portal = layout_.exitPos - Vec2{0.0f, 8.0f};
        Vec2 to = portal - player_->body.pos;
        if (to.length() < 12.0f) {
            exitHold_ += dt;
            player_->body.vel = player_->body.vel * 0.8f + to * 2.0f;
            if (exitHold_ > 0.5f && event_ == LevelEvent::None) {
                event_ = LevelEvent::ExitReached;
                audio_.play(Sfx::Portal, 0.8f);
            }
        } else {
            exitHold_ = 0.0f;
        }
    }

    updateIntensity();
    audio_.setListener(player_->body.pos);
}

void Level::handleSimEvents() {
    auto& events = world().events();
    if (events.empty()) return;
    // Burning gunpowder reports one event per grain; merge them into blasts per region.
    struct Cluster {
        Vec2 sum;
        int count = 0;
    };
    std::unordered_map<uint32_t, Cluster> clusters;
    for (const SimEvent& e : events) {
        if (e.type != SimEvent::Type::Explosion) continue;
        uint32_t key = (static_cast<uint32_t>(e.x / 14) << 16) | static_cast<uint32_t>(e.y / 14);
        Cluster& c = clusters[key];
        c.sum += Vec2{static_cast<float>(e.x), static_cast<float>(e.y)};
        ++c.count;
    }
    events.clear();
    for (auto& [key, c] : clusters) {
        ExplosionSpec spec;
        float n = static_cast<float>(c.count);
        spec.radius = std::min(26.0f, 4.0f + std::sqrt(n) * 2.2f);
        spec.damage = std::min(110.0f, 12.0f + n * 1.2f);
        spec.power = 150;
        spec.incendiary = true;
        spec.team = Team::Monster;
        spec.hurtsOwner = true;
        explode(c.sum / n, spec);
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
            if (greedy && dist < 70.0f && p.age > 0.3f) p.body.vel += to.normalized() * (900.0f * dt);
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
    case PickupType::Spell:
        if (!run_.addSpell(p.spell)) {
            audio_.play(Sfx::UiDeny, 0.6f);
            addFloatText(p.body.pos - Vec2{0.0f, 10.0f}, "Satchel full", 0xff8060);
            return;
        }
        pay();
        p.dead = true;
        audio_.play(Sfx::Pickup, 0.7f);
        addFloatText(p.body.pos - Vec2{0.0f, 8.0f}, spellDef(p.spell).name, 0xc0e0ff);
        break;
    case PickupType::Wand: {
        pay();
        Wand w = std::move(*p.wand);
        const Vec2 at = p.body.pos;
        p.dead = true; // `p` may dangle after the push_back below
        if (static_cast<int>(run_.wands.size()) < RunState::kMaxWands) {
            run_.wands.push_back(std::move(w));
            run_.activeWand = static_cast<int>(run_.wands.size()) - 1;
        } else {
            Wand old = std::move(run_.wands[static_cast<size_t>(run_.activeWand)]);
            run_.wands[static_cast<size_t>(run_.activeWand)] = std::move(w);
            pickups_.push_back(makeWandPickup(pl.body.pos, std::move(old)));
        }
        audio_.play(Sfx::Pickup, 0.8f, 0.8f);
        addFloatText(at - Vec2{0.0f, 8.0f}, run_.wands[static_cast<size_t>(run_.activeWand)].name, 0xffe0a0);
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
        dropLoot(p.body.pos - Vec2{0.0f, 4.0f}, std::min(4, layout_.levelIndex), true);
        addFlash(p.body.pos, 0xffd070, 2.0f, 60.0f, 0.5f);
        break;
    case PickupType::PerkAltar: {
        run_.applyPerk(p.perk);
        const PerkDef& def = perkDef(p.perk);
        for (Pickup& other : pickups_)
            if (other.type == PickupType::PerkAltar && other.altarGroup == p.altarGroup) other.dead = true;
        // Re-derive the player's stats from the run.
        const Vec2 pos = pl.body.pos;
        const uint32_t playerId = pl.id;
        player_ = std::make_unique<Player>(pos, run_);
        player_->id = playerId;
        player_->invulnerable = 0.0f;
        player_->hp = run_.hp;
        audio_.play(Sfx::Perk, 0.9f);
        addFloatText(pos - Vec2{0.0f, 14.0f}, def.name, def.color, 1.4f);
        addFlash(pos, def.color, 3.0f, 120.0f, 0.8f);
        particles_.burst(pos, 60, 160.0f, {255, 240, 200, 255}, {255, 200, 120, 255}, 0.8f, rng_, 0.0f);
        break;
    }
    default: break;
    }
}

void Level::updateEffects(float dt) {
    for (LightFlash& f : flashes_) f.life -= dt;
    flashes_.erase(std::remove_if(flashes_.begin(), flashes_.end(), [](const LightFlash& f) { return f.life <= 0.0f; }),
                   flashes_.end());
    for (FloatText& t : floatTexts_) {
        t.life -= dt;
        t.pos.y -= 18.0f * dt;
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

void Level::onPlayerDeath() {
    if (playerDeathHandled_) return;
    playerDeathHandled_ = true;
    run_.hp = 0.0f;
    audio_.play(Sfx::PlayerDie, 0.9f);
    bleed(player_->body.pos, mat::Blood, 60, {0.0f, -80.0f});
    particles_.burst(player_->body.pos, 40, 140.0f, {180, 140, 255, 255}, {120, 80, 255, 255}, 1.0f, rng_, 0.2f);
    addShake(0.7f);
    event_ = LevelEvent::PlayerDied;
}

} // namespace pm
