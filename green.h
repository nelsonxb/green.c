#ifndef GREEN_H
#define GREEN_H

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

#include <stddef.h>

/**
 * The green API is fairly simple:
 *  - Call `green_spawn()` to create a thread;
 *  - Call `green_resume()` repeatedly until it returns NULL;
 *  - Call `green_await()` from within a thread to pause execution.
 * 
 * When `green_resume()` is called, the thread's code will start running.
 * If `green_await()` is called, `green_resume()` will return
 * with the value passed in `wait_for`.
 * 
 * Subsequent calls to `green_resume()` will cause
 * `green_await()` within the thread to return
 * with the value passed in `resume_with`.
 * 
 * When the `start` function passed to `green_spawn()` returns,
 * `green_resume()` will free the thread and return NULL.
 * 
 * You will probably want to define
 * `struct gaio_await` and `struct gaio_resume`
 * somewhere in your own code.
 * These types are treated as opaque pointers by this API,
 * so feel free to use them however you see fit.
 * 
 * NOTE: The function passed to `green_spawn()` MUST make sure that when it returns,
 *       it has freed any resources it owns and removed any references
 *       to memory on the thread's stack!
 *       This is because the code calling `green_resume()`
 *       will not have a chance to deal with any of that
 *       before the stack is unmapped from memory
 *       by the time it learns that the thread is finished.
 */


#ifdef __cplusplus
extern "C" {
#endif


typedef struct gaio_await *green_await_t;
typedef struct gaio_resume *green_resume_t;
typedef struct _green_thread *green_thread_t;

green_thread_t green_spawn(void (*start)(void *arguments), void *arguments, size_t hint);
green_await_t green_resume(green_thread_t thread, green_resume_t resume_with);
green_resume_t green_await(green_await_t wait_for);

#define GREEN_RESUME_FAILED     ((green_await_t)&green_resume)
#define GREEN_AWAIT_FAILED      ((green_resume_t)&green_await)


#ifdef __cplusplus
}
#endif

#endif // include guard
