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
   wind-up and only begin from within view, low health pulses the screen with a
   heartbeat, and the death screen names what killed you and how to survive it next time.
3. **Short, replayable runs.** About 20-30 minutes, procedural levels, spell synergies,
   perks, vaults, unlockable starting paths, seeds and a daily rite.
4. **Respect the player.** Coyote time, step-up, levitation, generous invulnerability
   after hits, three difficulties, contextual hints instead of a manual, a map, runs that
   can be suspended, menus that work with mouse, keyboard or gamepad, a low graphics
   setting for weak GPUs, and levels that are verified traversable.

## A run

- **Stages** are 832×1472-cell caves (the first opens onto a night-sky mountainside).
  A guaranteed winding route leads to the exit portal; chambers, branches and pockets
  hang off it with chests, potions and free spells. A compass arrow follows that route a
  stretch ahead of the alchemist (`RouteGuide`), and the map marks the portal from the
  start - hollow until it has been seen.
- **Vaults** (up to two per stage) are set pieces stamped into solid rock off the route:
  an alchemist's laboratory with a cauldron, a powder magazine, a flooded crypt, a
  guarded treasury and a healing spring. Their passages are validated against the
  navigation grid so they are always reachable. The laboratory's worktable and the spring
  hold a *rare find* (`randomRareSpell`: an attack spell of tier 2-3 even on stage I, deeper
  later), which glints and shines gold; 5% of chest rolls are rare finds as well.
- **Guardians' lairs** (one on each of stages I-IV) are larger vaults holding an optional
  mini-boss: a giant of the stage's own kind, too big for the door and leashed to its
  hall, so the fight is always the player's choice. A slain keeper leaves a choice of two
  perk altars, its signature gift (fitting its stage's hazard) and one drawn at random that
  can never turn on the alchemist. It also yields its essence: four sockets under the stage name track them and the gathered ones orbit the
  alchemist; holding all four when the Rebis falls crowns the run with the Quintessence
  ending (and a feat). The Black Toad leaps with quakes and
  sheds slimes, the White Wraith looses fans of ice, the Mine King lobs dynamite and the
  Red Colossus slams and hurls fire. Below half health they enrage; slain, they grant a
  perk, and their hoard (a wand a tier finer than the stage's, carrying a rare spell,
  chests and gold) waits on a dais behind them. Lairs are
  marked on the map from the start.
- **Omens** give some stages a twist, announced on the title card: *Flooded*,
  *Overgrown*, *Volatile* (powder and oil everywhere), *Gilded* (rich gold veins and extra
  chests) or *Teeming* (more creatures, more treasure). The first stage draws only gentle
  omens (Flooded, Overgrown, Gilded) and the Athanor none.
- **Transmutation circles** crave a material; pour enough of it in with the flask to
  earn a perk. They teach the flask by rewarding it.
- **Sanctums** between stages heal fully and offer a shop (three spells and a wand,
  stocked and priced one tier deeper than the stage just cleared) and a choice of perks at
  altars.
- **The Athanor** is the boss arena: the Rebis has phases, summons and an enraged form.
- **Alchemical kills**: every hit records its cause, so a creature slain by the world
  rather than a spell's own blow (flames, lava, acid, a gunpowder blast, a cave-in,
  another creature) is named as it falls, counted in the run's stats, paid a little gold
  (a dozen times per stage) and chained with world kills that follow within half a second
  (Level::creditAlchemy). Three in one chain earn the *Chain Reaction* feat.
- **Elites** appear more often deeper down and always drop better loot. Each carries an
  affix shown by its aura: *Burning* (trails fire, immune to it, drops a fire spell),
  *Swift* (half again as fast) or *Gilded* (bleeds gold dust, treble gold).
- **Lanterns** hang along the route; a spell through one knocks it down to shatter into
  burning oil, so the scenery doubles as a trap.
- **Mimics** take the place of some route chests (never a vault's hoard): drawn, lit and
  lens-proof exactly like a chest, with one tell - now and then they breathe. They wake
  when the alchemist comes close (before "open" is offered) or when harmed, and pay out
  the chest's treasure when slain.
- **Stage events**: up to three times a stage the mountain stirs near the alchemist. A
  cave-in (grit trickles from the ceiling for a moment first), a breach that gushes the
  stage's liquid from a wall (water, brine, oil, sludge or lava), seeping marsh gas, a
  swarm out of view, or a fall of gold. The mix follows the stage and its omen; none come
  in sanctums, the Athanor, vaults or guardian fights.
- **Elixirs**: drinking quicksilver lends haste, ichor lends spells fury; both show on the
  HUD as counting-down status chips and on the alchemist's body.

## Progression across runs

- **Profile**: runs, victories, deepest stage, best time, kills, gold, chests.
- **Grimoire**: every spell discovered, creature slain (silhouettes until then) and perk
  taken, browsable from the title screen, plus a chronicle of the last ten runs (path,
  depth, time and fate); any of them can be replayed on the same seed.
- **Paths** (starting loadouts) unlock through milestones and change the opening:
  Pyromancer (reach stage II), Geomancer (open 8 chests), Stormcaller (slay 100
  creatures) and Hermeticist (slay a stage guardian; rolls a powerful wand per seed),
  paced so a first session opens one or two of them.
- **Ordeals** (after the first victory): eight stacking trials chosen when beginning the
  Work, each adding a rule to those beneath it - foes with more health, twice the elites,
  sanctums that heal only half, dearer shops, harder blows, a restless mountain (stage
  events twice as often), a frailer alchemist and wrathful keepers. Completing the Work
  under the highest ordeal open unlocks the next; the chronicle and end screen record it.
- **Daily Rite**: one seed per day with the Apprentice kit, so attempts are comparable
  (and never under an ordeal).
- **Suspended runs**: every stage start saves the run; Return to Title, Quit Game (or
  closing the window) resume there later. Abandon Run and Quit Game ask for a second press,
  and the pause menu's focus does not wrap onto them. Levels regenerate from their seed, so a resume replays the stage
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
  electricity and a pairwise reaction table. Nothing is left floating when the world is
  carved away: cut vines crumble into litter, felled trees shed whatever is no longer
  joined to their roots, and hanging lanterns drop and shatter into burning oil. Runs are
  deterministic for a seed (verified by identical bot runs on Linux and Windows builds).
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

- `pm_tests`: simulation invariants (conservation, reactions, sleeping, hanging vines),
  generation (route and vault reachability, determinism, omens, trees), spell table, deck
  casting, loadouts, run saves, the chronicle and death advice.
- `--autoplay --turbo [--god]`: a flow-field bot plays full runs headlessly; used for
  smoke tests, ASan runs and balance data (death causes per stage).
- CI builds and tests on Windows (MSVC) and Linux (GCC, warnings as errors) and runs the
  tests under ASan and UBSan with Clang; the Windows build is also exercised under Wine
  with MinGW.

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
