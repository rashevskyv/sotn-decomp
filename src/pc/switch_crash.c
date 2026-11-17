// SPDX-License-Identifier: AGPL-3.0-or-later
// libnx crash handler: logs the faulting addresses to sotn.log. Kept apart
// from switch.c because <switch.h> and <game.h> define the same type names.
// Find the code: offset = pc - anchor, then
// aarch64-none-elf-addr2line -e sotn.elf <nm address of Switch_Init + offset>
#include <switch.h>
#include <stdio.h>
#include <unistd.h>

const char* Switch_Init(void);

__attribute__((aligned(16))) u8 __nx_exception_stack[0x4000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

void __libnx_exception_handler(ThreadExceptionDump* ctx) {
    fprintf(stderr,
            "[crash] desc=0x%x pc=0x%lx lr=0x%lx far=0x%lx sp=0x%lx "
            "anchor Switch_Init=%p\n",
            ctx->error_desc, ctx->pc.x, ctx->lr.x, ctx->far.x, ctx->sp.x,
            (void*)Switch_Init);
    for (int i = 0; i < 29; i++) {
        fprintf(stderr, "[crash] x%d=0x%lx\n", i, ctx->cpu_gprs[i].x);
    }
    fflush(stderr);
    fsync(fileno(stderr));
    svcExitProcess();
}
