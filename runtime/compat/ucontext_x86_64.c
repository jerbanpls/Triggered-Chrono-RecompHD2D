/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * compat/ucontext.h for x86_64, from the System V AMD64 psABI: a call
 * preserves rbx, rbp, r12-r15 and rsp, and the control bits of MXCSR and
 * the x87 control word. A switch saves those, with the caller's return
 * address as the resume rip and rsp as it is after the return, and
 * resumes by loading the other set and jumping to its rip with eax = 0.
 *
 * regs[]: 0 rbx, 1 rbp, 2-5 r12-r15, 6 rsp, 7 rip, 8 MXCSR (low 4 bytes)
 * and the x87 control word (bytes 4-5).
 *
 * Only used where the C library has no makecontext; glibc builds use
 * glibc's unless -DCT_COMPAT_UCONTEXT=ON forces this one (for testing).
 */
#if defined(CT_COMPAT_UCONTEXT) && defined(__x86_64__) && !defined(_WIN32)

#include "ucontext.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

/* Called by the entry trampoline when a context's function returns and it
   has no uc_link. Hidden: the assembly calls it directly, even in a
   shared object. */
__attribute__((visibility("hidden"), noreturn, used)) void ct_compat_uc_returned(void)
{
    fputs("ct: a makecontext coroutine returned with no uc_link\n", stderr);
    fflush(stderr);
    abort();
}

__attribute__((visibility("hidden"))) void ct_compat_uc_trampoline(void);

/* SAVE stores into the context at %rdi: the resume point is the return
   address at (%rsp), the resumed rsp is just above it. */
#define CT_UC_SAVE \
    "movq %rbx, 0(%rdi)\n\t"  \
    "movq %rbp, 8(%rdi)\n\t"  \
    "movq %r12, 16(%rdi)\n\t" \
    "movq %r13, 24(%rdi)\n\t" \
    "movq %r14, 32(%rdi)\n\t" \
    "movq %r15, 40(%rdi)\n\t" \
    "leaq 8(%rsp), %rax\n\t"  \
    "movq %rax, 48(%rdi)\n\t" \
    "movq (%rsp), %rax\n\t"   \
    "movq %rax, 56(%rdi)\n\t" \
    "stmxcsr 64(%rdi)\n\t"    \
    "fnstcw 68(%rdi)\n\t"
/* LOAD restores from the context at r and jumps to its rip. */
#define CT_UC_LOAD(r) \
    "movq 0(" r "), %rbx\n\t"  \
    "movq 8(" r "), %rbp\n\t"  \
    "movq 16(" r "), %r12\n\t" \
    "movq 24(" r "), %r13\n\t" \
    "movq 32(" r "), %r14\n\t" \
    "movq 40(" r "), %r15\n\t" \
    "ldmxcsr 64(" r ")\n\t"    \
    "fldcw 68(" r ")\n\t"      \
    "movq 48(" r "), %rsp\n\t" \
    "xorl %eax, %eax\n\t"      \
    "jmpq *56(" r ")\n\t"

__asm__(
    ".text\n\t"
    ".p2align 4\n\t"
    ".globl ct_compat_getcontext\n\t"
    ".type ct_compat_getcontext, @function\n"
    "ct_compat_getcontext:\n\t"
    CT_UC_SAVE
    "xorl %eax, %eax\n\t"
    "ret\n\t"
    ".size ct_compat_getcontext, .-ct_compat_getcontext\n\t"

    ".p2align 4\n\t"
    ".globl ct_compat_swapcontext\n\t"
    ".type ct_compat_swapcontext, @function\n"
    "ct_compat_swapcontext:\n\t"
    CT_UC_SAVE
    CT_UC_LOAD("%rsi")
    ".size ct_compat_swapcontext, .-ct_compat_swapcontext\n\t"

    ".p2align 4\n\t"
    ".globl ct_compat_setcontext\n\t"
    ".type ct_compat_setcontext, @function\n"
    "ct_compat_setcontext:\n"
    ".Lct_uc_set:\n\t"
    CT_UC_LOAD("%rdi")
    ".size ct_compat_setcontext, .-ct_compat_setcontext\n\t"

    /* A fresh context starts here (its rip), rsp 16-byte aligned at the
       top of its stack, rbx its function and r12 its uc_link. The call
       pushes the return address, so the function is entered with
       rsp + 8 16-byte aligned, as after any call. */
    ".p2align 4\n\t"
    ".hidden ct_compat_uc_trampoline\n\t"
    ".type ct_compat_uc_trampoline, @function\n"
    "ct_compat_uc_trampoline:\n\t"
    "callq *%rbx\n\t"
    "movq %r12, %rdi\n\t"
    "testq %rdi, %rdi\n\t"
    "jnz .Lct_uc_set\n\t"
    "callq ct_compat_uc_returned\n\t"
    "ud2\n\t"
    ".size ct_compat_uc_trampoline, .-ct_compat_uc_trampoline\n\t");

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
    ucp->regs[2] = (uint64_t)(uintptr_t)ucp->uc_link; /* r12 */
    ucp->regs[6] = (uint64_t)top;                      /* rsp */
    ucp->regs[7] = (uint64_t)(uintptr_t)ct_compat_uc_trampoline; /* rip */
    /* regs[8], MXCSR and the x87 control word: as getcontext found them */
}

#else
typedef int ct_compat_ucontext_other_arch;   /* not empty: ISO C */
#endif
