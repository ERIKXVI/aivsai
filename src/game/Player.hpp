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

    float levitation() const { return levitation_; }
    float breath() const { return breath_; }
    Vec2 aimDir() const { return aimDir_; }
    Vec2 wandTip() const;
    bool facingRight() const { return facingRight_; }
    float castFlash() const { return castFlash_; }

protected:
    void onDamaged(Level& level, const DamageInfo& info, float dealt) override;
    void onKilled(Level& level, const DamageInfo& info) override;
    float burnDps() const override { return 7.0f; }

private:
    void move(Level& level, const Environment& env);
    void handleWands(Level& level);
    void handleFlask(Level& level);
    void collectGold(Level& level);

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
    float sprayAccum_ = 0.0f;
    float footstepTimer_ = 0.0f;
    float lastFallSpeed_ = 0.0f;
    bool wasInLiquid_ = false;
};

} // namespace pm
