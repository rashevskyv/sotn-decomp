// SPDX-License-Identifier: AGPL-3.0-or-later
// Saturn RSTAGE15 entity id -> rstage15.h EntityID (stage_saturn.c
// SaturnGdnBuildLayouts). The ids without a port stay idle.
static u16 SaturnGdnEntityId(u16 saturnId, bool saveRoom) {
    switch (saturnId) {
    case 0x01:
        return E_BREAKABLE;
    case 0x05:
        return E_RED_DOOR;
    case 0x0C: // room 2's item (d_prize_drops.c)
        return E_PERSISTENT_ITEM_DROP;
    case 0x0F:
        return saveRoom ? E_SATURN_MASONRY : E_DUMMY_0F;
    case 0x10:
        return E_SATURN_CONTROLLER;
    case 0x11:
        return E_ID_11;
    case 0x15:
        return E_WATER_TINT;
    case 0x16:
        return E_WATER_LINE;
    case 0x17:
        return E_ALUCARD_WATER_EFFECT;
    case 0x1E:
        return E_CAVE_TROLL;
    case 0x21:
        return E_MOD_COPPER;
    case 0x23:
        return E_MOD_EFREET;
    case 0x38:
        return E_MOD_GUARDIAN;
    case 0x2C:
        return E_VENUS_WEED;
    case 0x33:
        return E_BITTERFLY;
    case 0x34:
        return E_CLOAKED_KNIGHT;
    default:
        return E_DUMMY_0F;
    }
}
