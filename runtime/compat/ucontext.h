/* SPDX-FileCopyrightText: 2026 jerbanpls
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The ucontext calls the runtime's coroutines use (sched.c: the CPU on its
 * own stack; spc700_host.cpp: the SPC700 core), for C libraries that have
 * no makecontext/swapcontext: Android's bionic, musl.
 *
 * Without CT_COMPAT_UCONTEXT (the CMake option of the same name, off on
 * glibc) this is just <ucontext.h>. With it, getcontext, makecontext,
 * swapcontext and setcontext are our own (ucontext_<arch>.c), and
 * ucontext_t is ct_ucontext_t: the callee-saved registers of the
 * platform ABI, uc_stack and uc_link. The subset the runtime needs:
 *   - getcontext captures the caller; resuming that context returns from
 *     getcontext again (with 0);
 *   - makecontext takes a context from getcontext, its uc_stack, and a
 *     function of no arguments (argc must be 0); the function starts on
 *     that stack, 16-byte aligned. When it returns, uc_link is resumed if
 *     set; with no uc_link the program aborts with a message (glibc would
 *     exit the thread);
 *   - swapcontext saves into its first argument and resumes the second;
 *   - no signal mask is saved or restored (glibc's calls make a
 *     sigprocmask system call each; the runtime never changes the mask).
 * Supported: aarch64 (AAPCS64), x86_64 (System V) and 64-bit Windows (MinGW).
 */
#ifndef CT_COMPAT_UCONTEXT_H
#define CT_COMPAT_UCONTEXT_H

#ifndef CT_COMPAT_UCONTEXT
#include <ucontext.h>
#else

#include <stddef.h>
#include <stdint.h>
/* Any ucontext_t the C library declares (bionic's <signal.h> does) comes
   first, so the names below replace it from here on. */
#include <signal.h>

#if defined(__aarch64__)
/* x19-x28, x29 (fp), x30 (lr: where to resume), sp, d8-d15 */
#define CT_UC_NREGS 21
#elif defined(_WIN32) && defined(__x86_64__)
/* rbx, rbp, rdi, rsi, r12-r15, rsp, rip, MXCSR and the x87 control word,
   xmm6-xmm15 (two words each); ucontext_win32.c */
#define CT_UC_NREGS 31
#elif defined(__x86_64__)
/* rbx, rbp, r12-r15, rsp, rip, MXCSR and the x87 control word */
#define CT_UC_NREGS 9
#else
#error "CT_COMPAT_UCONTEXT: no context switch for this architecture (aarch64, x86_64)"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    void *ss_sp;
    size_t ss_size;
    int ss_flags;
} ct_stack_t;

typedef struct ct_ucontext {
    uint64_t regs[CT_UC_NREGS];   /* first: the switch code's offsets */
    struct ct_ucontext *uc_link;
    ct_stack_t uc_stack;
} ct_ucontext_t;

int ct_compat_getcontext(ct_ucontext_t *ucp);
int ct_compat_setcontext(const ct_ucontext_t *ucp);
int ct_compat_swapcontext(ct_ucontext_t *oucp, const ct_ucontext_t *ucp);
void ct_compat_makecontext(ct_ucontext_t *ucp, void (*fn)(void), int argc, ...);

#ifdef __cplusplus
}
#endif

#define ucontext_t ct_ucontext_t
#define getcontext ct_compat_getcontext
#define setcontext ct_compat_setcontext
#define swapcontext ct_compat_swapcontext
#define makecontext ct_compat_makecontext

#endif /* CT_COMPAT_UCONTEXT */
#endif
