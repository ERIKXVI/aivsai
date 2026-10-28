#include "sim/Material.hpp"

#include <cassert>

#include "core/Math.hpp"
#include "core/Rng.hpp"

namespace pm {

namespace detail {
// Constant-initialised (every member has a constant default), so there is no static-init-order hazard.
MaterialTables g_materialTables;
} // namespace detail

namespace {

MaterialTables& g_tables = detail::g_materialTables; // short name for the builders below
bool g_initialized = false;

/// Fluent helper that keeps the material table below readable.
class Define {
public:
    Define(MatId id, const char* name, Phase phase) : d_(g_tables.defs[id]), id_(id) {
        d_ = MaterialDef{};
        d_.name = name;
        d_.phase = phase;
        switch (phase) {
        case Phase::Empty: d_.density = 10; d_.opacity = 0; break;
        case Phase::Solid: d_.density = 1000; d_.hardness = 100; break;
        case Phase::Powder: d_.density = 200; d_.hardness = 20; d_.opacity = 240; break;
        case Phase::Liquid: d_.density = 100; d_.dispersion = 4; d_.hardness = 5; d_.opacity = 90; break;
        case Phase::Gas: d_.density = 5; d_.opacity = 30; break;
        case Phase::Fire: d_.density = 2; d_.opacity = 0; break;
        }
    }
    Define& density(int v) { d_.density = static_cast<uint16_t>(v); return *this; }
    Define& dispersion(int v) { d_.dispersion = static_cast<uint8_t>(v); return *this; }
    Define& hardness(int v) { d_.hardness = static_cast<uint8_t>(v); return *this; }
    Define& flammable(int chance, int burnTicks, MatId into = mat::Empty) {
        d_.flammability = static_cast<uint8_t>(chance);
        d_.burnTime = static_cast<uint8_t>(burnTicks);
        d_.burnsInto = into;
        return *this;
    }
    Define& life(int lo, int hi, MatId decaysInto = mat::Empty, int decayChance = 0) {
        d_.lifeMin = static_cast<uint8_t>(lo);
        d_.lifeMax = static_cast<uint8_t>(hi);
        d_.decaysInto = decaysInto;
        d_.decayChance = static_cast<uint8_t>(decayChance);
        return *this;
    }
    Define& damage(float dps) { d_.contactDamage = dps; return *this; }
    Define& flags(uint32_t f) { d_.flags |= f; return *this; }
    Define& colors(uint32_t a, uint32_t b, uint8_t alpha = 255) {
        d_.colorA = hexRgba(a, alpha);
        d_.colorB = hexRgba(b, alpha);
        return *this;
    }
    Define& glow(uint32_t c, uint8_t intensity) { d_.glow = hexRgba(c, intensity); return *this; }
    Define& opacity(int v) { d_.opacity = static_cast<uint8_t>(v); return *this; }
    /// Replaces the top `fraction` of the palette with a contrasting speck colour.
    Define& speckle(uint32_t c, float fraction) {
        speck_ = hexRgba(c, d_.colorA.a);
        speckFraction_ = fraction;
        return *this;
    }

    ~Define() { buildPalette(); }

private:
    void buildPalette() {
        auto& pal = g_tables.palette[id_];
        for (int i = 0; i < 256; ++i) {
            float t = static_cast<float>(i) / 255.0f;
            // Slight per-entry jitter breaks up banding in large flat areas.
            float jitter = (hashToUnit(hash2(id_, i, 77u)) - 0.5f) * 0.08f;
            float tt = saturate(t + jitter);
            Rgba c{static_cast<uint8_t>(lerp(d_.colorA.r, d_.colorB.r, tt)),
                   static_cast<uint8_t>(lerp(d_.colorA.g, d_.colorB.g, tt)),
                   static_cast<uint8_t>(lerp(d_.colorA.b, d_.colorB.b, tt)),
                   static_cast<uint8_t>(lerp(d_.colorA.a, d_.colorB.a, tt))};
            if (speckFraction_ > 0.0f && t > 1.0f - speckFraction_) c = speck_;
            pal[i] = c;
        }
    }

    MaterialDef& d_;
    MatId id_;
    Rgba speck_{};
    float speckFraction_ = 0.0f;
};

void react(MatId a, MatId b, MatId aInto, MatId bInto, int chance) {
    auto add = [](MatId self, MatId other, MatId selfInto, MatId otherInto, int c) {
        uint8_t& n = g_tables.reactionCount[self];
        assert(n < kMaxReactions && "raise kMaxReactions");
        if (n >= kMaxReactions) return;
        g_tables.reactions[self][n++] = Reaction{other, selfInto, otherInto, static_cast<uint8_t>(c)};
    };
    // Register on both participants so whichever cell updates first can trigger it.
    add(a, b, aInto, bInto, chance);
    add(b, a, bInto, aInto, chance);
}

void defineMaterials() {
    using namespace matflag;
    using namespace mat;

    Define(Empty, "Air", Phase::Empty);

    // --- Static solids -------------------------------------------------------------
    Define(Bedrock, "Bedrock", Phase::Solid).hardness(255).colors(0x1d1b24, 0x34303f).flags(kAcidProof);
    Define(Rock, "Rock", Phase::Solid).hardness(110).colors(0x302e38, 0x77737f);
    Define(Soil, "Soil", Phase::Solid).hardness(35).colors(0x34241a, 0x70513a).flags(kDiggable);
    Define(Wood, "Wood", Phase::Solid).hardness(45).colors(0x5a3a1e, 0x8a5f34)
        .flammable(14, 160, Ash).flags(kOrganic | kDiggable);
    Define(Brick, "Brick", Phase::Solid).hardness(140).colors(0x5e2c24, 0x8f4a3a);
    Define(Metal, "Metal", Phase::Solid).hardness(235).colors(0x4c5560, 0x8d99a6)
        .flags(kAcidProof | kConductive);
    Define(Ice, "Ice", Phase::Solid).hardness(55).colors(0x6aa8d0, 0xb8e2f8).opacity(140).flags(kMeltable);
    Define(Obsidian, "Obsidian", Phase::Solid).hardness(200).colors(0x110c18, 0x4a3264);
    Define(Basalt, "Basalt", Phase::Solid).hardness(150).colors(0x1c1715, 0x54443c);
    Define(Coal, "Coal", Phase::Solid).hardness(60).colors(0x222228, 0x44444e)
        .flammable(5, 255, Ash).speckle(0x55555e, 0.05f);
    Define(GoldOre, "Gold Ore", Phase::Solid).hardness(90).colors(0x4a4238, 0x6e6252).speckle(0xf2c84b, 0.18f);
    Define(Crystal, "Crystal", Phase::Solid).hardness(95).colors(0x3fa8d8, 0xb8f0ff).opacity(160)
        .glow(0x58c8ff, 110);
    Define(Glass, "Glass", Phase::Solid).hardness(70).colors(0x9fd0de, 0xcbeef6, 150).opacity(40)
        .flags(kAcidProof);
    Define(Moss, "Moss", Phase::Solid).hardness(20).colors(0x24451f, 0x4f8a37)
        .flammable(55, 40, Ash).flags(kOrganic | kDiggable);
    Define(Fungus, "Glowcap", Phase::Solid).hardness(25).colors(0x3c2a66, 0x8a68e0)
        .flammable(18, 60, Ash).glow(0x9a70ff, 70).flags(kOrganic | kDiggable);
    Define(SaltRock, "Salt Rock", Phase::Solid).hardness(70).colors(0x767c8a, 0xcfd5df);
    Define(Bone, "Bone", Phase::Solid).hardness(80).colors(0xb8ad94, 0xe2d8c0);
    Define(Flesh, "Flesh", Phase::Solid).hardness(30).colors(0x6a2a2e, 0x9a4a4a)
        .flammable(20, 80, Ash).flags(kOrganic | kDiggable);
    Define(Sulphite, "Sulphite", Phase::Solid).hardness(60).colors(0x8a7a1a, 0xc4ae36)
        .flammable(20, 120, ToxicGas);
    Define(Rune, "Runestone", Phase::Solid).hardness(255).colors(0x2c2448, 0x4a3c78)
        .glow(0x7a5cff, 40).flags(kAcidProof);
    Define(Vine, "Vine", Phase::Solid).hardness(5).colors(0x1f4a1c, 0x3f7a2c).opacity(60)
        .flammable(90, 30, Empty).flags(kOrganic | kDiggable | kNoCollide | kHangs);
    Define(Leaves, "Leaves", Phase::Solid).hardness(5).colors(0x1c3a22, 0x4a7a3a).opacity(110)
        .flammable(120, 25, Empty).flags(kOrganic | kDiggable | kNoCollide);
    Define(GlowVine, "Glowvine", Phase::Solid).hardness(5).colors(0x4a2a8a, 0x9a70f0).opacity(40)
        .glow(0x8a60ff, 50).flammable(70, 30, Empty).flags(kOrganic | kDiggable | kNoCollide | kHangs);

    // --- Powders --------------------------------------------------------------------
    Define(Sand, "Sand", Phase::Powder).density(200).colors(0xb08f4f, 0xe2c27e).flags(kDiggable);
    Define(Gravel, "Gravel", Phase::Powder).density(260).hardness(40).colors(0x4f4b47, 0x8c857c);
    Define(Ash, "Ash", Phase::Powder).density(120).colors(0x2f2d2c, 0x5e5a56).flags(kDiggable);
    Define(Snow, "Snow", Phase::Powder).density(110).colors(0xd0dce8, 0xffffff).flags(kMeltable | kDiggable);
    Define(Gunpowder, "Gunpowder", Phase::Powder).density(190).colors(0x1e1e22, 0x3c3836)
        .speckle(0x8a8070, 0.12f).flammable(255, 3, Smoke).flags(kExplosive);
    Define(GoldDust, "Gold", Phase::Powder).density(320).hardness(10).colors(0xb07f1f, 0xffe27a)
        .glow(0xffc840, 22).flags(kCollectible | kAcidProof);
    Define(Salt, "Salt", Phase::Powder).density(180).colors(0xcfd0c8, 0xf6f6f0);
    Define(Sulphur, "Sulphur", Phase::Powder).density(160).colors(0xa8901c, 0xeed64e)
        .flammable(110, 30, ToxicGas);
    Define(Spore, "Spores", Phase::Powder).density(60).colors(0x6a4aa8, 0xb090f0).glow(0x9a70ff, 30)
        .flammable(80, 10, Ash).flags(kOrganic);
    Define(Litter, "Plant Litter", Phase::Powder).density(50).colors(0x3b3020, 0x5f7032)
        .flammable(100, 15, Empty).flags(kOrganic | kDiggable);

    // --- Liquids --------------------------------------------------------------------
    Define(Water, "Water", Phase::Liquid).density(100).dispersion(6).colors(0x1f4e9a, 0x3570c4, 200)
        .flags(kExtinguisher | kConductive);
    Define(Oil, "Oil", Phase::Liquid).density(80).dispersion(4).colors(0x2a1e10, 0x4a3820, 230)
        .flammable(190, 50, Smoke).opacity(150);
    Define(Lava, "Lava", Phase::Liquid).density(180).dispersion(2).colors(0xc8340c, 0xffa030)
        .glow(0xff6418, 255).damage(70.0f).opacity(255).flags(kIgniter | kHot | kAcidProof);
    Define(Acid, "Acid", Phase::Liquid).density(110).dispersion(5).colors(0x4cc01a, 0x9cff3c, 215)
        .glow(0x80ff30, 60).damage(30.0f).flags(kCorrosive | kAcidProof);
    Define(Blood, "Blood", Phase::Liquid).density(104).dispersion(3).colors(0x5a0808, 0x8e1616, 230)
        .flags(kExtinguisher);
    Define(Slime, "Slime", Phase::Liquid).density(120).dispersion(1).colors(0x2a8a3a, 0x58c46a, 225)
        .flags(kSticky);
    Define(Vitae, "Vitae", Phase::Liquid).density(98).dispersion(5).colors(0xc83a7e, 0xff86c0, 215)
        .glow(0xff5aa8, 70).flags(kHealing | kExtinguisher);
    Define(Toxic, "Toxic Sludge", Phase::Liquid).density(115).dispersion(3).colors(0x5a7a0c, 0x96bc22, 235)
        .glow(0x9acc20, 35).damage(14.0f);
    Define(Midas, "Liquid Midas", Phase::Liquid).density(150).dispersion(4).colors(0xe8b020, 0xfff0a0, 240)
        .glow(0xffd040, 120).flags(kTransmuter | kAcidProof);
    Define(Quicksilver, "Quicksilver", Phase::Liquid).density(400).dispersion(5).colors(0x8a949e, 0xdce4ec)
        .opacity(255).flags(kConductive | kAcidProof);
    Define(Brine, "Brine", Phase::Liquid).density(103).dispersion(6).colors(0x3a6aa0, 0x5a8ac0, 200)
        .flags(kExtinguisher | kConductive);
    Define(Ichor, "Ichor", Phase::Liquid).density(106).dispersion(3).colors(0xd0901a, 0xffd060, 230)
        .glow(0xffa020, 90).flags(kExtinguisher);

    // --- Gases ----------------------------------------------------------------------
    Define(Smoke, "Smoke", Phase::Gas).density(6).colors(0x1a1a1c, 0x3a3a3e, 170).life(90, 200);
    Define(Steam, "Steam", Phase::Gas).density(3).colors(0xb8c4d0, 0xe8f0f8, 120).life(70, 170, Water, 70);
    Define(ToxicGas, "Toxic Gas", Phase::Gas).density(7).colors(0x5a8a18, 0x9ad040, 120).life(120, 240)
        .damage(10.0f);
    Define(Methane, "Swamp Gas", Phase::Gas).density(4).colors(0x6a7a4a, 0x8a9a6a, 60).life(200, 255)
        .flammable(255, 2, Fire);
    Define(Miasma, "Miasma", Phase::Gas).density(8).colors(0x4a1a5a, 0x8a3aa0, 150).life(120, 220)
        .glow(0xa040ff, 25).damage(18.0f);

    // --- Energy ---------------------------------------------------------------------
    Define(Fire, "Fire", Phase::Fire).colors(0xff5010, 0xffe060).glow(0xff7020, 255).life(6, 26, Smoke, 40)
        .damage(25.0f).flags(kIgniter | kHot);
    Define(Plasma, "Plasma", Phase::Fire).colors(0x80a0ff, 0xf0f8ff).glow(0x80b0ff, 255).life(4, 14)
        .damage(60.0f).flags(kIgniter | kHot);

    // --- Pairwise reactions ---------------------------------------------------------
    react(Water, Lava, Steam, Obsidian, 90);
    react(Brine, Lava, Steam, Obsidian, 90);
    react(Lava, Ice, kKeep, Water, 40);
    react(Lava, Snow, kKeep, Water, 80);
    react(Lava, Sand, kKeep, Glass, 3);
    react(Lava, Blood, kKeep, Smoke, 60);
    react(Lava, Toxic, kKeep, ToxicGas, 60);
    react(Lava, Slime, kKeep, Smoke, 30);
    react(Water, Salt, kKeep, Brine, 10);
    react(Water, SaltRock, Brine, kKeep, 1); // water saturates; the walls stay (and the pool settles)
    react(Salt, Ice, kKeep, Water, 12);
    react(Vitae, Toxic, Water, Water, 120);
    react(Vitae, Acid, Water, Water, 120);
    react(Water, Spore, kKeep, Fungus, 3);
    react(Quicksilver, GoldDust, kKeep, Midas, 2);
}

} // namespace

void initMaterials() {
    if (g_initialized) return;
    defineMaterials();
    g_initialized = true;
}

std::string matDescription(MatId id) {
    using namespace mat;
    switch (id) {
    case Water: return "Douses flames and cools lava into obsidian. Conducts electricity.";
    case Brine: return "Salt water: douses flames, cools lava and conducts electricity.";
    case Oil: return "Floats on water and burns fiercely.";
    case Lava: return "Burns whatever it touches. Water cools it into obsidian.";
    case Acid: return "Eats through stone and flesh. Vitae neutralises it.";
    case Blood: return "Puts out flames.";
    case Slime: return "Sticky: slows anything wading through it.";
    case Vitae: return "Heals whoever bathes in it or drinks it. Neutralises acid and sludge.";
    case Toxic: return "Eats at whatever wades through it. Drunk, it poisons for a while.";
    case Midas: return "Turns the stone it touches into gold ore.";
    case Quicksilver: return "Heavy, conductive metal. Turns gold dust into Liquid Midas. Drunk, it lends haste.";
    case Ichor: return "The glowing blood of fiends. Puts out flames. Drunk, it lends fury to your spells.";
    case Gunpowder: return "Explodes when lit, chaining into great blasts.";
    case GoldDust: return "Walk over it to collect it.";
    case GoldOre: return "Blast or dig it to free the gold dust inside.";
    case Sulphur: return "Burns readily into toxic gas.";
    case Salt: return "Turns water into brine and melts ice.";
    case Spore: return "Glowing spores; water sprouts them into glowcaps.";
    case Methane: return "Ignites in a flash.";
    case Steam: return "Hot vapour that condenses back into water.";
    case Fire: return "Spreads to anything that burns.";
    case Empty:
    case Rock:
    case Soil: return {};
    default: break;
    }
    const MaterialDef& d = g_tables.defs[id];
    constexpr uint32_t kNotable = matflag::kExplosive | matflag::kIgniter | matflag::kCorrosive | matflag::kHealing |
                                  matflag::kExtinguisher | matflag::kSticky | matflag::kTransmuter | matflag::kCollectible;
    const bool unbreakable = d.phase == Phase::Solid && d.hardness == 255;
    if (!(d.flags & kNotable) && d.contactDamage <= 0.0f && !unbreakable) return {};
    std::string out;
    auto add = [&](const char* s) {
        if (!out.empty()) out += ' ';
        out += s;
    };
    if (unbreakable) add("Unbreakable.");
    if (d.flags & matflag::kExplosive) add("Explosive.");
    if (d.flags & matflag::kIgniter) add("Sets things alight.");
    if (d.contactDamage > 0.0f) add("Harmful to touch.");
    if (d.flags & matflag::kCorrosive) add("Corrosive.");
    if (d.flags & matflag::kHealing) add("Heals.");
    if (d.flags & matflag::kExtinguisher) add("Puts out flames.");
    if (d.flags & matflag::kSticky) add("Sticky.");
    if (d.flammability > 0) add(d.flammability > 150 ? "Highly flammable." : "Burns.");
    if (d.flags & matflag::kMeltable) add("Melts near heat.");
    if (d.flags & matflag::kConductive) add("Conducts electricity.");
    if (d.glow.a > 0 && d.phase == Phase::Solid) add("Glows.");
    return out;
}

Cell makeCell(MatId id, uint32_t randomBits) {
    Cell c;
    c.mat = id;
    c.var = static_cast<uint8_t>(randomBits & 0xFF);
    const MaterialDef& d = g_tables.defs[id];
    if (d.lifeMax > 0) {
        int span = d.lifeMax - d.lifeMin + 1;
        c.life = static_cast<uint8_t>(d.lifeMin + static_cast<int>((randomBits >> 8) % static_cast<uint32_t>(span)));
    }
    if ((randomBits >> 20) & 1u) c.flags |= cellflag::kFlowRight;
    return c;
}

} // namespace pm
