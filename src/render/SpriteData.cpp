// Pixel-art sprite sources. Every sprite is ASCII art; see the palette below.
// Sprites face right; the renderer mirrors them for left-facing creatures.

#include <iterator>

#include "render/Sprites.hpp"

namespace pm {

namespace {

struct PaletteEntry {
    char c;
    Color color;
    bool emissive;
};

// clang-format off
constexpr PaletteEntry kPalette[] = {
    // Shared outlines
    {'o', {16, 12, 22, 255}, false},   {'O', {40, 26, 18, 255}, false},
    // Alchemist
    {'H', {50, 32, 82, 255}, false},   {'h', {80, 54, 126, 255}, false},  {'j', {118, 86, 168, 255}, false},
    {'f', {10, 8, 16, 255}, false},    {'E', {140, 245, 255, 255}, true}, {'r', {72, 48, 114, 255}, false},
    {'R', {46, 30, 76, 255}, false},   {'l', {112, 80, 158, 255}, false}, {'g', {228, 182, 72, 255}, false},
    {'b', {44, 30, 26, 255}, false},   {'s', {172, 52, 52, 255}, false},
    // Slime
    {'G', {28, 104, 48, 255}, false},  {'v', {58, 166, 78, 255}, false},  {'V', {132, 228, 128, 255}, false},
    {'w', {232, 255, 232, 255}, false},{'e', {10, 30, 12, 255}, false},
    // Bat
    {'k', {38, 28, 40, 255}, false},   {'m', {88, 58, 80, 255}, false},   {'M', {130, 88, 110, 255}, false},
    {'Y', {255, 214, 80, 255}, true},
    // Ghoul miner
    {'z', {118, 138, 106, 255}, false},{'Z', {78, 94, 72, 255}, false},   {'c', {92, 70, 50, 255}, false},
    {'C', {60, 46, 34, 255}, false},   {'x', {170, 170, 182, 255}, false},{'X', {112, 80, 42, 255}, false},
    {'Q', {255, 64, 40, 255}, true},   {'L', {255, 232, 150, 255}, true},
    // Imp
    {'i', {196, 52, 30, 255}, false},  {'I', {118, 28, 20, 255}, false},  {'F', {255, 148, 40, 255}, true},
    {'W', {255, 240, 150, 255}, true},
    // Wraith
    {'a', {150, 198, 232, 230}, false},{'A', {78, 118, 170, 230}, false}, {'n', {150, 242, 255, 255}, true},
    {'u', {236, 246, 255, 240}, false},
    // Shroom
    {'p', {124, 70, 176, 255}, false}, {'P', {80, 42, 120, 255}, false},  {'q', {206, 166, 255, 255}, true},
    {'t', {212, 200, 180, 255}, false},{'T', {160, 148, 128, 255}, false},
    // Crystal turret / golem stone
    {'y', {80, 204, 226, 255}, false}, {'D', {78, 74, 90, 255}, false},   {'d', {122, 118, 134, 255}, false},
    {'K', {186, 250, 255, 255}, true}, {'6', {255, 142, 60, 255}, true},
    // Beetle
    {'B', {38, 38, 50, 255}, false},   {'N', {72, 72, 94, 255}, false},   {'J', {255, 184, 60, 255}, true},
    {'1', {26, 26, 30, 255}, false},
    // Worm
    {'2', {150, 88, 100, 255}, false}, {'3', {98, 54, 66, 255}, false},   {'4', {204, 142, 152, 255}, false},
    {'5', {242, 232, 210, 255}, false},
    // Homunculus
    {'7', {222, 150, 148, 255}, false},{'8', {160, 94, 96, 255}, false},
    // Props
    {'9', {62, 58, 64, 255}, false},   {'U', {255, 204, 112, 255}, true}, {'0', {186, 186, 196, 255}, false},
    {'%', {196, 228, 244, 200}, false},{'@', {255, 255, 255, 255}, false},{'+', {255, 255, 255, 255}, true},
    {'#', {150, 110, 255, 255}, true}, {'*', {255, 255, 255, 255}, true}, {'$', {255, 214, 90, 255}, true},
    {'S', {96, 90, 104, 255}, false},  {'^', {142, 136, 150, 255}, false},
};
// clang-format on

// ---- Alchemist (9x13) ------------------------------------------------------------------
const char* const kPlayerIdle[] = {
    // frame 0
    ".oo......",
    "ohHo.....",
    ".oHHoo...",
    ".oHhhHo..",
    "oHhjfffo.",
    "oHhffEfo.",
    "oRrhfffo.",
    "oRrrrrRo.",
    "oRgsgsgRo",
    "oRrrrrlRo",
    ".oRrrrlRo",
    ".oRrrrrRo",
    "..obo.bo.",
    // frame 1 (breath)
    ".........",
    ".oo......",
    "ohHo.....",
    ".oHHHoo..",
    "oHhjfffo.",
    "oHhffEfo.",
    "oRrhfffo.",
    "oRrrrrRo.",
    "oRgsgsgRo",
    "oRrrrrlRo",
    ".oRrrrlRo",
    ".oRrrrrRo",
    "..obo.bo.",
};

const char* const kPlayerRun[] = {
    // frame 0
    ".........",
    "oo.......",
    "ohHoo....",
    ".oHHhHo..",
    "oHhjfffo.",
    "oHhffEfo.",
    "oRrhfffo.",
    "oRrrrrRo.",
    "oRgsgsgRo",
    "oRrrrrlRo",
    ".oRrrrlRo",
    "obRrrRbo.",
    "oo....oo.",
    // frame 1
    ".oo......",
    "ohHo.....",
    ".oHHoo...",
    ".oHhhHo..",
    "oHhjfffo.",
    "oHhffEfo.",
    "oRrhfffo.",
    "oRrrrrRo.",
    "oRgsgsgRo",
    "oRrrrrlRo",
    ".oRrrrlRo",
    ".oRrbRRo.",
    "...oboo..",
    // frame 2
    ".........",
    "oo.......",
    "ohHoo....",
    ".oHHhHo..",
    "oHhjfffo.",
    "oHhffEfo.",
    "oRrhfffo.",
    "oRrrrrRo.",
    "oRgsgsgRo",
    "oRrrrrlRo",
    ".oRrrrlRo",
    ".obRRrbo.",
    ".oo...oo.",
    // frame 3
    ".oo......",
    "ohHo.....",
    ".oHHoo...",
    ".oHhhHo..",
    "oHhjfffo.",
    "oHhffEfo.",
    "oRrhfffo.",
    "oRrrrrRo.",
    "oRgsgsgRo",
    "oRrrrrlRo",
    ".oRrrrlRo",
    ".oRRbrRo.",
    "..oobo...",
};

const char* const kPlayerAir[] = {
    // frame 0 (rising)
    "oo.......",
    "ohHoo....",
    ".oHHhHo..",
    ".oHhjfffo",
    "oHhhffEfo",
    "oRrhhfffo",
    "oRrrrrRo.",
    "oRgsgsgRo",
    "oRrrrrlRo",
    "oRrrrrlRo",
    "oRrRrRrRo",
    ".oboob.o.",
    ".........",
    // frame 1 (robe flare)
    "oo.......",
    "ohHoo....",
    ".oHHhHo..",
    ".oHhjfffo",
    "oHhhffEfo",
    "oRrhhfffo",
    "oRrrrrRo.",
    "oRgsgsgRo",
    "oRrrrrlRo",
    "ORrrrrlRO",
    "ORrRrRrRrO",
    "o.ob.bo..o",
    ".........",
};

// ---- Slime (12x8) ----------------------------------------------------------------------
const char* const kSlime[] = {
    "............",
    "....oooo....",
    "..ooVVvvoo..",
    ".oVwVvvvvvo.",
    ".ovvevvevvvo",
    "ovvvvvvvvvGo",
    "oGvvvvvvvGGo",
    ".ooooooooooo",
    // squash
    "............",
    "............",
    "...oooooo...",
    "..oVwVvvvvo.",
    ".ovvevvevvvo",
    "ovvvvvvvvvGo",
    "oGGvvvvvvGGo",
    "oooooooooooo",
    // stretch
    ".....oo.....",
    "...ooVvoo...",
    "..oVwvvvvo..",
    "..ovevvevo..",
    ".ovvvvvvvvo.",
    ".oGvvvvvvGo.",
    ".oGGvvvvGGo.",
    "..oooooooo..",
};

// ---- Bat (13x7) -----------------------------------------------------------------------
const char* const kBat[] = {
    "mo.........om",
    "Mmo.......omM",
    "MmMo.oko.oMmM",
    ".MmmokYkommM.",
    "..ooookkooo..",
    ".....okko....",
    "......oo.....",
    // wings down
    ".............",
    ".....oko.....",
    "....okYko....",
    "..ooookkooo..",
    ".oMmmokkommo.",
    "oMmMo.oo.oMmo",
    "Mmo.......omM",
};

// ---- Ghoul miner (10x13) -------------------------------------------------------------
const char* const kGhoul[] = {
    "..oooo....",
    ".oCCCLo...",
    ".oCCCCo...",
    ".ozzQzo..X",
    ".ozZzzo.xX",
    "..oZzo..X.",
    ".occcco.X.",
    "occcczzzX.",
    "occccco.X.",
    ".occcco...",
    ".oCCCCo...",
    ".oZo.oZo..",
    ".oo..oo...",
    // step
    "..oooo....",
    ".oCCCLo...",
    ".oCCCCo...",
    ".ozzQzo..X",
    ".ozZzzo.xX",
    "..oZzo..X.",
    ".occcco.X.",
    "occcczzzX.",
    "occccco.X.",
    ".occcco...",
    ".oCCCCo...",
    "..oZoZo...",
    "..oo.oo...",
    // swing
    "..oooo..x.",
    ".oCCCLoxXx",
    ".oCCCCoX..",
    ".ozzQzoX..",
    ".ozZzzX...",
    "..oZzX....",
    ".occczz...",
    "occcccc...",
    "occccco...",
    ".occcco...",
    ".oCCCCo...",
    ".oZo.oZo..",
    ".oo..oo...",
};

// ---- Imp (10x11) ----------------------------------------------------------------------
const char* const kImp[] = {
    "..F....F..",
    ".FWo..oWF.",
    "..oiooio..",
    ".oiiiiiio.",
    ".oiWiiWio.",
    ".oiiIIiio.",
    "oFoiiiioFo",
    "FWoiiiioWF",
    ".o.oIIo.o.",
    "...FWWF...",
    "....FF....",
    // flicker
    ".F......F.",
    "..WFo.oFW.",
    "..oiooio..",
    ".oiiiiiio.",
    ".oiWiiWio.",
    ".oiiIIiio.",
    "FoiiiiiioF",
    "WFoiiiioFW",
    ".o.oIIo.o.",
    "....FWF...",
    "...FWF....",
};

// ---- Wraith (11x14) -----------------------------------------------------------------
const char* const kWraith[] = {
    "...ooooo...",
    "..oaaaaao..",
    ".oaAAAAAao.",
    ".oAnAAnAAo.",
    ".oaAAAAAao.",
    "..oaaaaao..",
    ".oaauuuaao.",
    "oaauuuuuaao",
    "oaAauuuaAao",
    ".oAaauaaAo.",
    ".oaAaaaAao.",
    "..oa.a.ao..",
    "..a..a..a..",
    "...........",
    // drift
    "...ooooo...",
    "..oaaaaao..",
    ".oaAAAAAao.",
    ".oAnAAnAAo.",
    ".oaAAAAAao.",
    "..oaaaaao..",
    ".oaauuuaao.",
    "oaauuuuuaao",
    "oaAauuuaAao",
    ".oAaauaaAo.",
    ".oaAaaaAao.",
    "..ao.a.oa..",
    "...a.a.a...",
    "..a.....a..",
};

// ---- Spore shroom (12x12) ---------------------------------------------------------------
const char* const kShroom[] = {
    "....oooo....",
    "..oopppPoo..",
    ".opqppqpppo.",
    "opppPppqpPPo",
    "oPqppppppqPo",
    "oPPPPPPPPPPo",
    ".oooTttToo..",
    "....oQtQo...",
    "....ottTo...",
    "....otTTo...",
    "...ottttTo..",
    "...oooooooo.",
    // puff
    "...oooooo...",
    ".oopppPppoo.",
    "opqppqpppqpo",
    "opppPppqpPPo",
    "oPqppppppqPo",
    "oPPPPPPPPPPo",
    ".oooTttToo..",
    "....oQtQo...",
    "....ottTo...",
    "....otTTo...",
    "...ottttTo..",
    "...oooooooo.",
};

// ---- Crystal turret (11x13) -----------------------------------------------------------
const char* const kTurret[] = {
    ".....o.....",
    "....oyo....",
    "...oyKyo...",
    "...oyKyo...",
    "..oyyKyyo..",
    "..oyKKKyo..",
    "..oyyKyyo..",
    "...oyyyo...",
    "..oDdddDo..",
    ".oDddddddo.",
    ".oDdDDDdDo.",
    "oDDDDDDDDDo",
    "ooooooooooo",
    // charged
    ".....o.....",
    "....oKo....",
    "...oKKKo...",
    "...oyKyo...",
    "..oKyKyKo..",
    "..oyKKKyo..",
    "..oKyKyKo..",
    "...oyKyo...",
    "..oDdddDo..",
    ".oDddddddo.",
    ".oDdDDDdDo.",
    "oDDDDDDDDDo",
    "ooooooooooo",
};

// ---- Bomb beetle (11x7) --------------------------------------------------------------
const char* const kBeetle[] = {
    "......J....",
    ".....oJo...",
    "...ooNNoo..",
    "..oNNNNNNo.",
    ".oBNNNNNNBo",
    "oQBBBBBBBBo",
    ".1.1.1.1.1.",
    // walk
    ".....J.....",
    ".....oJo...",
    "...ooNNoo..",
    "..oNNNNNNo.",
    ".oBNNNNNNBo",
    "oQBBBBBBBBo",
    "1.1.1.1.1..",
};

// ---- Worm (10x10 head, 8x8 body) --------------------------------------------------------
const char* const kWormHead[] = {
    "..oooooo..",
    ".o244442o.",
    "o24422442o",
    "o2Q4224Q2o",
    "o22222222o",
    "o23333332o",
    "o3o5o5o53o",
    ".o5o5o5oo.",
    "..o.o.o...",
    "..........",
};
const char* const kWormBody[] = {
    "..oooo..",
    ".o2442o.",
    "o244442o",
    "o222222o",
    "o232232o",
    "o333333o",
    ".o3333o.",
    "..oooo..",
};

// ---- Golem (14x16) ------------------------------------------------------------------
const char* const kGolem[] = {
    "....oooooo....",
    "...odddddDo...",
    "...od6dd6Do...",
    "...oddddDDo...",
    "..oooDDDDooo..",
    ".odddooooodddo",
    "oddDdddddddDdo",
    "odDDdd66ddDDdo",
    "odDodd66ddoDdo",
    "oddoddddddoddo",
    ".oo.oDDDDo.oo.",
    "....odddDo....",
    "...oddoodDo...",
    "...odDo.oDDo..",
    "..oddDo.oddDo.",
    "..oooo...oooo.",
    // step
    "....oooooo....",
    "...odddddDo...",
    "...od6dd6Do...",
    "...oddddDDo...",
    "..oooDDDDooo..",
    ".odddooooodddo",
    "oddDdddddddDdo",
    "odDDdd66ddDDdo",
    "odDodd66ddoDdo",
    "oddoddddddoddo",
    ".oo.oDDDDo.oo.",
    "....odddDo....",
    "....oddoDo....",
    "...oddo.oDo...",
    "...oddo.oDDo..",
    "...ooo...ooo..",
};

// ---- Homunculus (9x10) -----------------------------------------------------------
const char* const kHomunculus[] = {
    "..ooooo..",
    ".o77777o.",
    "o7E777E7o",
    "o7777777o",
    "o78o8o87o",
    ".o77777o.",
    "o8777778o",
    "o.o777o.o",
    "..o8o8o..",
    "..oo.oo..",
    // hop
    "..ooooo..",
    ".o77777o.",
    "o7E777E7o",
    "o7777777o",
    "o78o8o87o",
    ".o77777o.",
    ".o87778o.",
    "o8o777o8o",
    ".o8o.o8o.",
    "..o...o..",
};

// ---- Props -------------------------------------------------------------------------
const char* const kLantern[] = {
    "...o...",
    "...9...",
    "..o9o..",
    ".o999o.",
    ".9UUU9.",
    ".9UWU9.",
    ".9UUU9.",
    ".o999o.",
    "..ooo..",
    // flicker
    "...o...",
    "...9...",
    "..o9o..",
    ".o999o.",
    ".9UWU9.",
    ".9UUU9.",
    ".9UUU9.",
    ".o999o.",
    "..ooo..",
};

const char* const kChest[] = {
    ".oooooooooo.",
    "oXXXXXXXXXXo",
    "oXOXXXXXXOXo",
    "oggggg0ggggo",
    "oXOXX00XXOXo",
    "oXXXXXXXXXXo",
    "oXOXXXXXXOXo",
    "oooooooooooo",
};
const char* const kChestOpen[] = {
    ".oooooooooo.",
    "oXOXXXXXXOXo",
    "oggggggggggo",
    "o$$$$$$$$$$o",
    "oXOXX00XXOXo",
    "oXXXXXXXXXXo",
    "oXOXXXXXXOXo",
    "oooooooooooo",
};

const char* const kPotion[] = {
    "..ooo..",
    "..oXo..",
    "..o%o..",
    ".o%@%o.",
    "o%@@@%o",
    "o@@@@@o",
    "o@@@@@o",
    ".o@@@o.",
    "..ooo..",
};

const char* const kSpellCard[] = {
    "ooooooo",
    "o+++++o",
    "o+@@@+o",
    "o+@+@+o",
    "o+@@@+o",
    "o+++++o",
    "o+@+@+o",
    "o+++++o",
    "ooooooo",
};

const char* const kPortal[] = {
    "....oooooo....",
    "..oo##*###oo..",
    ".o#*######*#o.",
    "o##########*#o",
    "o#####**#####o",
    "o####*++*####o",
    "o###*++++*###o",
    "o###*++++*###o",
    "o####*++*####o",
    "o#####**#####o",
    "o#*##########o",
    ".o#########*o.",
    "..oo###*##oo..",
    "....oooooo....",
    // frame 1
    "....oooooo....",
    "..oo###*##oo..",
    ".o#########*o.",
    "o#*##########o",
    "o#####**#####o",
    "o####*++*####o",
    "o###*++++*###o",
    "o###*++++*###o",
    "o####*++*####o",
    "o#####**#####o",
    "o##########*#o",
    ".o#*######*#o.",
    "..oo##*###oo..",
    "....oooooo....",
};

const char* const kAltar[] = {
    "......+......",
    ".....+++.....",
    "......+......",
    "..ooooooooo..",
    ".oSSSSSSSSSo.",
    "..oS^S#S^So..",
    "..oS^S#S^So..",
    "..oS^SSS^So..",
    ".oSSSSSSSSSo.",
    "ooooooooooooo",
};

const char* const kPedestal[] = {
    ".ooooooooo.",
    "oS^^^^^^^So",
    ".oSSSSSSSo.",
    "..oS^^^So..",
    "..oS^^^So..",
    ".oSSSSSSSo.",
    "ooooooooooo",
};

// ---- Boss: the Rebis, the two-headed alchemical hermaphrodite (24x22) --------------------
const char* const kBoss[] = {
    "....oooo.......oooo.....",
    "...o7777o.....o7777o....",
    "..o7E77E7o...o7E77E7o...",
    "..o777777o...o777777o...",
    "..o78oo87o...o78oo87o...",
    "...o7777o.....o7777o....",
    "....o88ooooooooo88o.....",
    "...o8777777777777778o...",
    "..o877gggggggggggg778o..",
    ".o8777gggggggggggg7778o.",
    ".o877ggg*gg##gg*ggg778o.",
    "o8777ggggg####ggggg7778o",
    "o8777ggggg####ggggg7778o",
    "o8777ggg*gg##gg*ggg7778o",
    ".o8777gggggggggggg7778o.",
    ".o8777gggggggggggg7778o.",
    "..o87777777777777778o...",
    "...o88777777777777o.....",
    "....oo8o77oo77o8oo......",
    "......o8o8oo8o8o........",
    ".....o8oo8oo8oo8o.......",
    ".....oo..oo..oo.oo......",
};
const char* const kBossCore[] = {
    "..oooo..",
    ".o$##$o.",
    "o$#**#$o",
    "o#*++*#o",
    "o#*++*#o",
    "o$#**#$o",
    ".o$##$o.",
    "..oooo..",
};

#define PM_SPRITE(id, w, h, rows) SpriteSource{SpriteId::id, w, h, static_cast<int>(std::size(rows)) / (h), rows}

const SpriteSource kSources[] = {
    PM_SPRITE(PlayerIdle, 9, 13, kPlayerIdle), PM_SPRITE(PlayerRun, 9, 13, kPlayerRun),
    PM_SPRITE(PlayerAir, 10, 13, kPlayerAir),  PM_SPRITE(Slime, 12, 8, kSlime),
    PM_SPRITE(Bat, 13, 7, kBat),               PM_SPRITE(Ghoul, 10, 13, kGhoul),
    PM_SPRITE(Imp, 10, 11, kImp),              PM_SPRITE(Wraith, 11, 14, kWraith),
    PM_SPRITE(Shroom, 12, 12, kShroom),        PM_SPRITE(Turret, 11, 13, kTurret),
    PM_SPRITE(Beetle, 11, 7, kBeetle),         PM_SPRITE(WormHead, 10, 10, kWormHead),
    PM_SPRITE(WormBody, 8, 8, kWormBody),      PM_SPRITE(Golem, 14, 16, kGolem),
    PM_SPRITE(Homunculus, 9, 10, kHomunculus), PM_SPRITE(Lantern, 7, 9, kLantern),
    PM_SPRITE(Chest, 12, 8, kChest),           PM_SPRITE(ChestOpen, 12, 8, kChestOpen),
    PM_SPRITE(Potion, 7, 9, kPotion),          PM_SPRITE(SpellCard, 7, 9, kSpellCard),
    PM_SPRITE(Portal, 14, 14, kPortal),        PM_SPRITE(Altar, 13, 10, kAltar),
    PM_SPRITE(Pedestal, 11, 7, kPedestal),     PM_SPRITE(Boss, 24, 22, kBoss),
    PM_SPRITE(BossCore, 8, 8, kBossCore),
};

#undef PM_SPRITE

} // namespace

const SpriteSource* spriteSources(int& count) {
    count = static_cast<int>(std::size(kSources));
    return kSources;
}

Color spritePaletteColor(char c, bool& emissive) {
    for (const PaletteEntry& e : kPalette) {
        if (e.c == c) {
            emissive = e.emissive;
            return e.color;
        }
    }
    emissive = false;
    return Color{0, 0, 0, 0};
}

} // namespace pm
