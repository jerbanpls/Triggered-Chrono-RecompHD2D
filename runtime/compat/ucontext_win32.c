/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * compat/ucontext.h for 64-bit Windows (MinGW), from the Microsoft x64
 * calling convention: a call preserves rbx, rbp, rdi, rsi, r12-r15,
 * xmm6-xmm15 and rsp, and the control bits of MXCSR and the x87 control
 * word. A switch saves those, with the caller's return address as the
 * resume rip and rsp as it is after the return, and resumes by loading the
 * other set and jumping to its rip with eax = 0.
 *
 * port: not Windows Fibers (ConvertThreadToFiber / SwitchToFiber), which
 * the first plan named. A fiber cannot do what the runtime's contract and
 * test/test_ucontext.c need: getcontext resumed a second time (a point in
 * the middle of a running stack) and uc_link resuming such a point. A
 * register switch can, as on the other platforms. What it does not do is
 * keep the TEB's stack bounds for the new stack, so nothing may unwind
 * (RtlUnwindEx) across a coroutine: CMakeLists.txt defines
 * __USE_MINGW_SETJMP_NON_SEH for Windows, which makes longjmp a plain
 * register restore.
 *
 * regs[]: 0 rbx, 1 rbp, 2 rdi, 3 rsi, 4-7 r12-r15, 8 rsp, 9 rip,
 * 10 MXCSR (low 4 bytes) and the x87 control word (bytes 4-5),
 * 11-30 xmm6-xmm15 (two words each).
 */
#if defined(CT_COMPAT_UCONTEXT) && defined(_WIN32) && defined(__x86_64__)

#include "ucontext.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* Called by the entry trampoline when a context's function returns and it
   has no uc_link. */
__attribute__((noreturn, used)) void ct_compat_uc_returned(void)
{
    fputs("ct: a makecontext coroutine returned with no uc_link\n", stderr);
    fflush(stderr);
    abort();
}

void ct_compat_uc_trampoline(void);

/* SAVE stores into the context at %rcx: the resume point is the return
   address at (%rsp), the resumed rsp is just above it. */
#define CT_UC_SAVE \
    "movq %rbx, 0(%rcx)\n\t"   \
    "movq %rbp, 8(%rcx)\n\t"   \
    "movq %rdi, 16(%rcx)\n\t"  \
    "movq %rsi, 24(%rcx)\n\t"  \
    "movq %r12, 32(%rcx)\n\t"  \
    "movq %r13, 40(%rcx)\n\t"  \
    "movq %r14, 48(%rcx)\n\t"  \
    "movq %r15, 56(%rcx)\n\t"  \
    "leaq 8(%rsp), %rax\n\t"   \
    "movq %rax, 64(%rcx)\n\t"  \
    "movq (%rsp), %rax\n\t"    \
    "movq %rax, 72(%rcx)\n\t"  \
    "stmxcsr 80(%rcx)\n\t"     \
    "fnstcw 84(%rcx)\n\t"      \
    "movups %xmm6, 88(%rcx)\n\t"   \
    "movups %xmm7, 104(%rcx)\n\t"  \
    "movups %xmm8, 120(%rcx)\n\t"  \
    "movups %xmm9, 136(%rcx)\n\t"  \
    "movups %xmm10, 152(%rcx)\n\t" \
    "movups %xmm11, 168(%rcx)\n\t" \
    "movups %xmm12, 184(%rcx)\n\t" \
    "movups %xmm13, 200(%rcx)\n\t" \
    "movups %xmm14, 216(%rcx)\n\t" \
    "movups %xmm15, 232(%rcx)\n\t"
/* LOAD restores from the context at r and jumps to its rip. */
#define CT_UC_LOAD(r) \
    "movq 0(" r "), %rbx\n\t"   \
    "movq 8(" r "), %rbp\n\t"   \
    "movq 16(" r "), %rdi\n\t"  \
    "movq 24(" r "), %rsi\n\t"  \
    "movq 32(" r "), %r12\n\t"  \
    "movq 40(" r "), %r13\n\t"  \
    "movq 48(" r "), %r14\n\t"  \
    "movq 56(" r "), %r15\n\t"  \
    "ldmxcsr 80(" r ")\n\t"     \
    "fldcw 84(" r ")\n\t"       \
    "movups 88(" r "), %xmm6\n\t"   \
    "movups 104(" r "), %xmm7\n\t"  \
    "movups 120(" r "), %xmm8\n\t"  \
    "movups 136(" r "), %xmm9\n\t"  \
    "movups 152(" r "), %xmm10\n\t" \
    "movups 168(" r "), %xmm11\n\t" \
    "movups 184(" r "), %xmm12\n\t" \
    "movups 200(" r "), %xmm13\n\t" \
    "movups 216(" r "), %xmm14\n\t" \
    "movups 232(" r "), %xmm15\n\t" \
    "movq 64(" r "), %rsp\n\t"  \
    "xorl %eax, %eax\n\t"       \
    "jmpq *72(" r ")\n\t"

__asm__(
    ".text\n\t"
    ".p2align 4\n\t"
    ".globl ct_compat_getcontext\n\t"
    "ct_compat_getcontext:\n\t"
    CT_UC_SAVE
    "xorl %eax, %eax\n\t"
    "ret\n\t"

    ".p2align 4\n\t"
    ".globl ct_compat_swapcontext\n\t"
    "ct_compat_swapcontext:\n\t"
    CT_UC_SAVE
    CT_UC_LOAD("%rdx")

    ".p2align 4\n\t"
    ".globl ct_compat_setcontext\n\t"
    "ct_compat_setcontext:\n"
    ".Lct_uc_set:\n\t"
    CT_UC_LOAD("%rcx")

    /* A fresh context starts here (its rip), rsp 16-byte aligned at the
       top of its stack, rbx its function and r12 its uc_link. The 32
       bytes are the callee's shadow space; the call pushes the return
       address, so the function is entered with rsp + 8 16-byte aligned,
       as after any call. */
    ".p2align 4\n\t"
    "ct_compat_uc_trampoline:\n\t"
    "subq $32, %rsp\n\t"
    "callq *%rbx\n\t"
    "movq %r12, %rcx\n\t"
    "testq %rcx, %rcx\n\t"
    "jnz .Lct_uc_set\n\t"
    "callq ct_compat_uc_returned\n\t"
    "ud2\n\t");

void ct_compat_makecontext(ct_ucontext_t *ucp, void (*fn)(void), int argc, ...)
{
    if (argc != 0) {
        fprintf(stderr, "ct: makecontext with %d arguments (only 0 supported)\n", argc);
        abort();
    }
    uintptr_t top = (uintptr_t)ucp->uc_stack.ss_sp + ucp->uc_stack.ss_size;
    top &= ~(uintptr_t)15;
    ucp->regs[0] = (uint64_t)(uintptr_t)fn;           /* rbx */
    ucp->regs[1] = 0;                                  /* rbp: end of the frame chain */
    ucp->regs[4] = (uint64_t)(uintptr_t)ucp->uc_link; /* r12 */
    ucp->regs[8] = (uint64_t)top;                      /* rsp */
    ucp->regs[9] = (uint64_t)(uintptr_t)ct_compat_uc_trampoline; /* rip */
    /* regs[10] and up, MXCSR, the x87 control word and xmm6-15: as getcontext found them */
}

#else
typedef int ct_compat_ucontext_other_arch;   /* not empty: ISO C */
#endif
