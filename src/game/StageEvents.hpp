#pragma once
// Stage events: now and then the mountain stirs around the alchemist. A stretch of ceiling
// caves in, a wall breaches and floods the passage, marsh gas seeps from a crack, a swarm comes
// out of the dark or a seam of gold gives way overhead. Each is announced and telegraphed for a
// moment before it lands, so it reads as a hazard (or a windfall) rather than a cheap shot, and
// no two descents through a stage play out quite the same.

#include <cstdint>
#include <string>
#include <utility>

#include "core/Math.hpp"
#include "sim/Cell.hpp"

namespace pm {

class Level;
class Rng;

enum class StageEventKind : uint8_t { CaveIn, Breach, GasSeep, Swarm, GoldFall, Count };

/// The line that announces an event, in its own colour.
struct EventNotice {
    std::string text;
    uint32_t color = 0xffffff;
};

class StageEvents {
public:
    /// Arms the stage's first event; sanctums and the Athanor stay quiet.
    void begin(const Level& level, Rng& rng);
    void tick(Level& level);

    /// The notice for the event just begun (empty text when none); the scene shows it once.
    EventNotice takeNotice() { return std::exchange(notice_, {}); }
    bool active() const { return phase_ != Phase::Idle; }
    StageEventKind kind() const { return kind_; }
    /// Where the event comes from: the ceiling, crack or roost.
    Vec2 site() const { return site_; }

    /// Starts `kind` at once if a site for it lies near the alchemist (tests and previews).
    bool trigger(Level& level, StageEventKind kind);

private:
    enum class Phase : uint8_t { Idle, Warning, Unfolding };

    bool start(Level& level, StageEventKind kind);
    StageEventKind pickKind(const Level& level, Rng& rng) const;
    /// The telegraph: grit, sparks and tremors at the site.
    void warn(Level& level);
    /// The moment it lands: the ceiling lets go, the crack opens, the swarm arrives.
    void release(Level& level);
    void unfold(Level& level);
    /// Turns the ceiling above `site_` loose: stone falls as debris (and gold, for a gold fall).
    void collapse(Level& level, int radiusX, int radiusY, bool gold);

    bool enabled_ = false;
    float pace_ = 1.0f;      ///< scales the gaps between events (the Restless Mountain halves them)
    int maxEvents_ = 0;
    float countdown_ = 0.0f; ///< seconds until the next event is due
    int fired_ = 0;
    Phase phase_ = Phase::Idle;
    StageEventKind kind_ = StageEventKind::CaveIn;
    float timer_ = 0.0f;   ///< seconds left in the current phase
    float elapsed_ = 0.0f; ///< seconds since the event landed
    bool released_ = false;
    Vec2 site_;
    Vec2 impact_;            ///< where a cave-in's rock lands
    float strikeAt_ = -1.0f; ///< ... and how long after the collapse it does
    float dirX_ = 1.0f;      ///< which way a breach or seep gushes
    MatId matter_ = 0;       ///< what a breach or seep lets out
    EventNotice notice_;
};

} // namespace pm
