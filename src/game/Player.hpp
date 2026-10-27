#pragma once
// The alchemist: platforming with coyote time and levitation, wand casting, the material
// flask (absorb / pour / drink), gold collection and swimming.

#include "game/Actor.hpp"
#include "game/Input.hpp"

namespace pm {

struct RunState;

class Player final : public Actor {
public:
    Player(Vec2 pos, RunState& run);

    void setInput(const PlayerInput& input) { input_ = input; }
    /// Re-derives health and resistances from the run (after a perk), keeping everything else.
    void refreshFromRun();
    void tick(Level& level) override;
    void drawAlbedo(DrawContext& ctx) const override;
    void drawEmission(DrawContext& ctx) const override;
    void addLights(DrawContext& ctx) const override;
    /// The alchemist's light dying with them: `k` runs from 1 down to 0 over the death fade.
    void addFadingLight(DrawContext& ctx, float k) const;

    float levitation() const { return levitation_; }
    float breath() const { return breath_; }
    Vec2 aimDir() const { return aimDir_; }
    Vec2 wandTip() const;
    /// What the alchemist's spells deal relative to their base damage (perks, difficulty, fury).
    float damageMul() const;
    bool facingRight() const { return facingRight_; }
    float castFlash() const { return castFlash_; }
    /// The camera nudge (cells) from casts since the last call, scaled by their weight.
    Vec2 takeRecoil() {
        const Vec2 r = recoil_;
        recoil_ = {};
        return r;
    }

protected:
    void onDamaged(Level& level, const DamageInfo& info, float dealt) override;
    void onKilled(Level& level, const DamageInfo& info) override;
    float burnDps() const override { return 7.0f; }

private:
    void move(Level& level, const Environment& env);
    /// Footfall sounds while walking, pitched by the ground underfoot.
    void footsteps(Level& level, float dt);
    void handleWands(Level& level);
    /// The wand coughs grey sparks instead of casting: it lacks the mana.
    void fizzle(Level& level);
    /// A burst of sparks in the spell's colour from the wand tip (fewer for rapid-fire wands).
    void castSparks(Level& level, float angle, uint32_t color, bool rapid);
    /// The plunge into a liquid: its sound, and droplets thrown up from the surface.
    void splash(Level& level, MatId liquid);
    void handleFlask(Level& level);
    void collectGold(Level& level);
    void drawEssences(DrawContext& ctx, Vec2 p) const;

    RunState& run_;
    PlayerInput input_;
    Vec2 aimDir_{1.0f, 0.0f};
    bool facingRight_ = true;
    float coyote_ = 0.0f;
    float jumpBuffer_ = 0.0f;
    float levitation_ = 1.0f; ///< fraction of the run's levitation time remaining
    bool jumpedThisPress_ = false;
    float breath_ = 1.0f;
    float drownTimer_ = 0.0f;
    uint32_t nextFlaskNotice_ = 0; ///< tick before which the "flask holds..." notice stays quiet
    float animTime_ = 0.0f;
    float castFlash_ = 0.0f;
    float squash_ = 0.0f; ///< >0 squashed by a landing, <0 stretched by a jump; eases back to 0
    Vec2 recoil_;
    float sprayAccum_ = 0.0f;
    float footstepTimer_ = 0.0f; ///< counts down to the next footfall sound
    bool levitating_ = false;     ///< the levitation has engaged during this hold (the whoosh plays as it starts)
    float lastFallSpeed_ = 0.0f;
    bool wasInLiquid_ = false;
};

} // namespace pm
