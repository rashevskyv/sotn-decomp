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
    default:
        return E_DUMMY_0F;
    }
}
