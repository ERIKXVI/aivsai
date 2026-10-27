#pragma once
// Title screen. The background is a live simulation: the title is carved in gold and
// runestone while sand, water and lava pour over it and react, lit by the full pipeline.

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "app/Persistence.hpp"
#include "app/Scene.hpp"
#include "game/Loadouts.hpp"
#include "game/Run.hpp"
#include "render/Background.hpp"
#include "render/SceneRenderer.hpp"
#include "sim/Particles.hpp"
#include "sim/World.hpp"
#include "ui/GrimoirePanel.hpp"
#include "ui/Ui.hpp"

namespace pm {

class MenuScene final : public Scene {
public:
    explicit MenuScene(App& app);

    void tick() override;
    void frame(float dt) override;
    void render(float alpha) override;

private:
    enum class Panel { Main, Loadout, Grimoire, Settings, Help };

    void buildWorld();
    void startRun(bool daily, LoadoutId loadout);
    /// Starts a new run on a chronicled run's seed, path and difficulty.
    void replayRun(const RunHistoryEntry& entry);
    /// Folds a suspended run into the profile before a new one replaces it.
    void abandonSuspendedRun();
    /// Lifetime progress for the path unlocks, counting a suspended run: starting anew records it first.
    Milestones milestones() const;
    void drawMain();
    void drawLoadouts();
    /// The ordeal chooser under the paths (or a note that it opens after the first victory).
    void drawOrdealRow(Rectangle row, std::string& tipTitle, std::string& tipBody);
    /// The gold rim glows faintly and a slanted glint sweeps across it; rim cells acid has eaten stay dark.
    void stampRimGlow(WorldRenderer& layer) const;

    std::unique_ptr<World> world_;
    ParticleSystem particles_;
    SceneRenderer scene_;
    Background background_;
    Rng rng_;
    Panel panel_ = Panel::Main;
    GrimoirePanel grimoire_;
    ui::NavList nav_;
    std::optional<RunState> suspended_; ///< run that "Continue the Work" resumes
    /// Spells on each kit's signature wand, built once (empty for random kits).
    std::array<std::vector<SpellId>, kLoadoutCount> kitPreview_;
    bool confirmDaily_ = false;         ///< Daily Rite pressed once with a run suspended
    bool confirmBegin_ = false;         ///< Begin pressed once on Choose Your Path with a run suspended
    bool usingGamepad_ = false;         ///< the last input came from a gamepad (the help shows its buttons)
    float time_ = 0.0f;
    float nextEvent_ = 2.0f;
    int titleTop_ = 0, titleBottom_ = 0;
    int titleLeft_ = 0, titleRight_ = 0;
    std::vector<Vec2i> rim_; ///< the title's gold rim cells, which glow and catch a passing glint
};

} // namespace pm
