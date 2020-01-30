/* This file is part of https://github.com/NelsonCrosby/green.c */
/* BSD 2-Clause License                                                      *
 *                                                                           *
 * Copyright (c) 2020, Nelson Crosby <nelson@tika.to>                        *
 * All rights reserved.                                                      *
 *                                                                           *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "green.h"


#if defined(__linux__)
 #define S_LINUX 1
 #if defined(__x86_64__)
  #define A_LX64 1
  #define A_ARM64 0
  #define A_ARM32 0
 #elif defined(__aarch64__)
  #define A_ARM64 1
  #define A_LX64 0
  #define A_ARM32 0
  #error TODO: AArch64 support
 #elif defined(__arm__)
  #define A_ARM32 1
  #define A_LX64 0
  #define A_ARM64 0
  #error TODO: ARM support
 #else
  #define A_LX64 0
  #define A_ARM64 0
  #define A_ARM32 0
  #error unsupported target architecture
 #endif
#else
 #define S_LINUX 0
 #error unsupported target operating system
#endif


struct _green_thread {
    green_thread_t last_active;
};


#ifdef _GREEN_ASM_DEBUG
 #ifndef _GREEN_EXPORT_INTERNALS
  #define _GREEN_EXPORT_INTERNALS
 #endif
#else
 #if A_LX64
  asm(".include \"green.x86_64.s\"");
 #endif
#endif

#ifdef _GREEN_EXPORT_INTERNALS
 #define _STATIC extern
#else
 #define _STATIC static
#endif

_STATIC green_thread_t *__attribute__((used))
_green_current()
{
    static __thread green_thread_t current = NULL;
    return &current;
}

_STATIC green_thread_t __attribute__((used))
_green_thread_activate(green_thread_t new_active)
{
    if ((new_active - 1)->last_active != new_active) {
        return NULL;
    }

    green_thread_t *active = _green_current();
    (new_active - 1)->last_active = *active;
    *active = new_active;
    return new_active;
}

_STATIC green_thread_t __attribute__((used))
_green_thread_deactivate()
{
    green_thread_t *active = _green_current();
    green_thread_t old = *active;
    if (old == NULL) {
        return NULL;
    }

    *active = (old - 1)->last_active;
    (old - 1)->last_active = old;
    return old;
}
