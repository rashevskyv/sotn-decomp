// SPDX-License-Identifier: AGPL-3.0-or-later
/*
 * The area name banner of a Saturn stage (Saturn entity 0x09), drawn as
 * every other stage's: the PS1 banner (e_stage_name_us.h, its box, fade and
 * dissolve) on VRAM 64,256 with the CLUTs 0x19F and 0x19D. A Saturn stage
 * loads nothing there, so the banner uploads its 1x page first (the stage's
 * banner_page.h, en or uk). The HD pictures replace that page by its hash
 * (tools/lang/stage_names_hd.py --saturn). Define STAGE_FLAG first.
 */
#define EntityStageNamePopup StageNamePopup
#include "e_stage_name_us.h"
#undef EntityStageNamePopup
#include "banner_page.h"

extern int g_Lang;

void EntityModBanner(Entity* self) {
    static u16 page[32 * 128];
    RECT rect = {64, 256, 32, 128};
    const u16* src = g_Lang ? s_BannerPageUk : s_BannerPageEn;

    if (!self->step && !g_CastleFlags[STAGE_FLAG]) {
        memcpy(page, src, sizeof(page[0]) * 32 * BANNER_PAGE_ROWS);
        memcpy(page + 32 * 64, src + 32 * BANNER_PAGE_ROWS,
               sizeof(page[0]) * 32 * BANNER_PAGE_ROWS);
        LoadImage(&rect, (u_long*)page);
    }
    StageNamePopup(self);
}
