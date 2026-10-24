# PRIMA MATERIA — Design Document

> *Prima materia*: the alchemists' "first matter", the formless substance from which
> everything is made. In this game, every pixel is matter, and all of it is simulated.

## Pitch

A falling-sand action roguelite. You are an apprentice alchemist descending through a
living mountain toward the Magnum Opus. Every pixel of the world is a simulated
material — sand pours, water flows, oil burns, lava cools into obsidian, acid eats
through stone, gunpowder detonates. Your wands throw spells that carve, burn, flood
and transmute the world; your flask absorbs and sprays any liquid or powder.

The descent follows the four stages of the alchemical Great Work:

| Stage | Biome | Palette / Feel | Signature materials |
|-------|-------|----------------|---------------------|
| I. Nigredo | The Blackened Caverns | charcoal, moss green, black water | coal, moss, oil, wood |
| II. Albedo | The Salt Glaciers | white, pale blue, crystal | ice, snow, salt, brine |
| III. Citrinitas | The Sulphur Mines | gold, ochre, toxic yellow | sulphur, gunpowder, gold, fungus |
| IV. Rubedo | The Crimson Forge | red, black basalt, magma | lava, basalt, obsidian, fire |
| V. Magnum Opus | The Athanor | the boss arena | everything |

Between stages the player rests in a **Sanctum**: buy spells/wands, heal, pick a perk.

## Core pillars

1. **The world is the weapon.** Every system talks to the material simulation.
2. **Readable chaos.** Strong lighting, color coding, and juice keep the chaos legible.
3. **Short, replayable runs.** ~30 minutes, procedural levels, spell synergies, perks,
   seeds and a daily run.
4. **Respect the player.** Generous controls (coyote time, step-up, levitation),
   difficulty options, no unfair one-shots on the default difficulty.

## Technology

- C++20, raylib 5.5 (fetched by CMake), OpenGL 3.3, no external assets: all art,
  sprites, sound and music are generated procedurally at startup.
- Fixed 60 Hz simulation, interpolated rendering (smooth on high-refresh displays).
- Chunked cellular automaton (64×64 chunks, dirty rects) updated on a thread pool
  with a 4-phase checkerboard schedule.
- Rendering at a low "cell" resolution (≈480×270) with a deferred-style 2D lighting
  pipeline (emissive glow, shadowed point lights, solid-depth shading, bloom),
  composited at screen resolution with a sharp-bilinear upscale.

## Systems overview

- `sim/`     materials, cells, world grid, chunk scheduler, reactions, particles, explosions
- `gen/`     noise, biome generators, prefabs, level connectivity validation
- `game/`    player, enemies, projectiles, spells, wands, perks, pickups, run state
- `render/`  world upload, sprites, lighting, post-processing, backgrounds
- `audio/`   sfx synthesizer, generative music
- `ui/`      menus, HUD, wand editor

## Controls (keyboard + mouse)

| Action | Key |
|--------|-----|
| Move | A / D |
| Jump / levitate (hold) | W or Space |
| Aim | Mouse |
| Cast wand | Left mouse |
| Spray flask / absorb | Right mouse / hold Q |
| Drink flask | F |
| Select wand | 1–4, mouse wheel |
| Interact / pick up | E |
| Inventory & wand editing | Tab |
| Pause | Esc |
