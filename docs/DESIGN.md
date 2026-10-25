# PRIMA MATERIA — Design Document

> *Prima materia*: the alchemists' "first matter", the formless substance from which
> everything is made. In this game, every pixel is matter, and all of it is simulated.

## Pitch

A falling-sand action roguelite. You are an apprentice alchemist descending through a
living mountain toward the Magnum Opus. Every pixel of the world is a simulated
material: sand pours, water flows, oil burns, lava cools into obsidian, acid eats
through stone, gunpowder detonates. Your wands throw spells that carve, burn, flood
and transmute the world; your flask absorbs, pours and can be drunk.

The descent follows the four stages of the alchemical Great Work:

| Stage | Biome | Palette / feel | Signature materials |
|-------|-------|----------------|---------------------|
| I. Nigredo | The Blackened Caverns | charcoal, moss green, black water | coal, moss, oil, wood |
| II. Albedo | The Salt Glaciers | white, pale blue, crystal | ice, salt rock, brine |
| III. Citrinitas | The Sulphur Mines | gold, ochre, toxic yellow | sulphur, gunpowder, gold |
| IV. Rubedo | The Crimson Forge | red, black basalt, magma | lava, basalt, obsidian, fire |
| V. Magnum Opus | The Athanor | the boss arena | the Rebis |

## Core pillars

1. **The world is the weapon.** Every system talks to the material simulation: spells
   carve and ignite it, the flask carries it, creatures bleed into it, perks are earned
   by feeding it to transmutation circles.
2. **Readable chaos.** Strong lighting, colour coding, hit-stop, shockwaves and clear
   HUD feedback keep the chaos legible. Ranged attacks are telegraphed by a glowing
   wind-up, low health pulses the screen with a heartbeat, and the death screen names
   what killed you.
3. **Short, replayable runs.** About 20-30 minutes, procedural levels, spell synergies,
   perks, vaults, unlockable starting paths, seeds and a daily rite.
4. **Respect the player.** Coyote time, step-up, levitation, generous invulnerability
   after hits, three difficulties, contextual hints instead of a manual, a map, runs that
   can be suspended, menus that work with mouse, keyboard or gamepad, a low graphics
   setting for weak GPUs, and levels that are verified traversable.

## A run

- **Stages** are 832×1472-cell caves (the first opens onto a night-sky mountainside).
  A guaranteed winding route leads to the exit portal; chambers, branches and pockets
  hang off it with chests, potions and free spells. A compass arrow and the map point
  the way.
- **Vaults** (up to two per stage) are set pieces stamped into solid rock off the route:
  an alchemist's laboratory with a cauldron, a powder magazine, a flooded crypt, a
  guarded treasury and a healing spring. Their passages are validated against the
  navigation grid so they are always reachable.
- **Transmutation circles** crave a material; pour enough of it in with the flask to
  earn a perk. They teach the flask by rewarding it.
- **Sanctums** between stages heal fully and offer a shop (three spells and a wand,
  priced for the depth) and a choice of perks at altars.
- **The Athanor** is the boss arena: the Rebis has phases, summons and an enraged form.
- **Elites** (golden aura) appear more often deeper down and always drop better loot.

## Progression across runs

- **Profile**: runs, victories, deepest stage, best time, kills, gold, chests.
- **Grimoire**: every spell discovered, creature slain (silhouettes until then) and perk
  taken, browsable from the title screen.
- **Paths** (starting loadouts) unlock through milestones and change the opening:
  Pyromancer (reach stage 3), Geomancer (open 12 chests), Stormcaller (slay 250
  creatures) and Hermeticist (complete the Great Work, rolls a powerful wand per seed).
- **Daily Rite**: one seed per day with the Apprentice kit, so attempts are comparable.
- **Suspended runs**: every stage start saves the run; Save & Quit (or closing the window)
  resumes there later. Levels regenerate from their seed, so a resume replays the stage
  from its start rather than duplicating loot already taken.

## Onboarding

- A controls card for the first half minute of the first stage.
- Contextual hints, each shown once per launch (and switchable off): absorbing liquids, slotting a new
  spell, healing with Vitae, sanctums, circles, the compass, gold ore and (immediately,
  because it kills) how to douse flames. Button prompts follow the last-used device.
- How to Play summarises controls and the rules of matter; the Alchemy Sandbox lets
  players experiment with every material, fire and blasts without danger.

## Technology

- C++20, raylib 5.5 (fetched by CMake), OpenGL 3.3, no external assets: art, sprites,
  sound and music are generated procedurally at startup. The release is one
  self-contained executable with a static runtime.
- Fixed 60 Hz simulation with interpolated rendering; input is latched between ticks so
  nothing is lost at high frame rates or during hit-stop.
- **Simulation**: 6-byte cells in 64×64 chunks with dirty rectangles; each tick updates
  dirty chunks in four checkerboard phases on a fork-join thread pool. A cell never moves
  far enough to touch a chunk updated in the same phase, so no locking is needed. Rules
  cover powders, liquids (density, viscosity), gases, fire, burning, corrosion, melting,
  electricity and a pairwise reaction table. Runs are deterministic for a seed (verified
  by identical bot runs on Linux and Windows builds).
- **Rendering**: the world is drawn at cell resolution (≈480×270) into albedo and
  emission layers. A light pass ray-marches up to 64 point lights through the occlusion
  field (emissive matter such as lava becomes lights automatically), a dual-Kawase
  pyramid adds glow, and the composite runs at screen resolution with sharp-bilinear
  scaling, solid-depth shading, ACES tonemapping, shockwave distortion and aberration.
- **Generation**: domain-warped noise caves plus a planned route, chambers, branches,
  biome painting, vegetation, structures, pools, pre-simulation so liquids settle, then a
  connectivity repair loop and vault placement, all checked with a flow-field NavGrid.
- **Audio**: an sfxr-style synthesiser renders every effect; music is generative (pads,
  bass, Karplus-Strong plucks, drums) in each biome's mode, following combat intensity.

## Code map

- `src/sim/`    materials, cells, world grid and chunk scheduler, reactions, particles
- `src/gen/`    biomes, level generator, vaults, navigation grid
- `src/game/`   player, enemies, projectiles, spells, wands, perks, loadouts, pickups,
                level logic, level renderer, camera, test bot
- `src/render/` lighting pipeline, world layer upload, sprites, sky, backgrounds
- `src/audio/`  sound synthesis and generative music
- `src/ui/`     immediate-mode widgets, settings, Grimoire and map panels
- `src/scenes/` menu, run (HUD, inventory, end screens, hints) and sandbox
- `src/app/`    window, main loop, input latch, settings, profile and run saves

## Quality

- `pm_tests`: simulation invariants (conservation, reactions, sleeping), generation
  (route and vault reachability, determinism), spell table, deck casting and loadouts.
- `--autoplay --turbo [--god]`: a flow-field bot plays full runs headlessly; used for
  smoke tests, ASan runs and balance data (death causes per stage).
- CI builds and tests on Windows (MSVC) and Linux; the Windows build is also exercised
  under Wine with MinGW.

## Controls

| Action | Keyboard & mouse | Gamepad |
|--------|------------------|---------|
| Move | A / D | Left stick |
| Jump / levitate (hold) | W or Space | A |
| Aim / cast | Mouse / left button | Right stick / RT |
| Pour flask / absorb | Right button / hold Q | LT / Y |
| Drink flask | F | X |
| Switch wand | 1–4, mouse wheel | LB / RB |
| Interact / pick up / buy | E | B |
| Edit wands | Tab | Back |
| Map | M | D-pad down |
| Pause | Esc | Start |
