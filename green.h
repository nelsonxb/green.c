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

/** \file
 * The green API is fairly simple:
 *  - Call \ref green_spawn to create a coroutine;
 *  - Call \ref green_resume repeatedly until it returns `NULL`;
 *  - Call \ref green_await from within a coroutine to pause execution.
 *
 * Coroutines in green are essentially just an independent stack.
 * The functions \ref green_resume and \ref green_await
 * simply switch into and out of those stacks.
 *
 * Green itself is no more than this.
 * By defining `struct gaio_await` and `struct gaio_resume`
 * (see \ref green_await_t and \ref green_resume_t),
 * you can define your own system for handling
 * when and why these switches happen.
 *
 * You are encouraged to read the detailed documentation
 * for the full API (there's not much of it),
 * but here's a short version:
 *
 * - Define `struct gaio_await` and `struct gaio_resume`;
 * - Write a function that looks like:
 *   ```c
 *   void my_coro_entrypoint(void *arguments)
 *   {...}
 *   ```
 * - Call \ref green_await from some functions
 *   that get called by `my_coro_entrypoint`;
 * - In your main program, call \ref green_spawn
 *   with `my_coro_entrypoint`;
 * - Call \ref green_resume to start the coroutine;
 * - Call \ref green_resume with various values
 *   based on the value the last resume returned.
 */


#ifdef __cplusplus
extern "C" {
#endif


/**
 * Type indicating why a coroutine has awaited.
 *
 * This type is treated as an opaque pointer by green,
 * so you may define `struct gaio_await` however you see fit.
 *
 * It should include whatever information is needed
 * to determine what event should cause the coroutine to resume.
 */
typedef struct gaio_await *green_await_t;

/**
 * Type representing the result of an awaited operation.
 *
 * This type is treated as an opaque pointer by green,
 * so you may define `struct gaio_resume` however you see fit.
 *
 * It should include whatever information is needed
 * to determine the result of an await operation.
 * If you need to be able to cancel a coroutine,
 * your definition of `struct gaio_resume` should contain
 * some way of notifying the coroutine of this occurrence.
 */
typedef struct gaio_resume *green_resume_t;

/**
 * Handle to a coroutine.
 *
 * This type must be treated as an opaque pointer/handle.
 * It is needed to resume a coroutine.
 *
 * The resources associated with a coroutine are released
 * once the associated `start` function returns.
 */
typedef struct _green_thread *green_thread_t;

/** Coroutine entrypoint. */
typedef void (*green_start_t)(void *arguments);


/**
 * Create a new coroutine.
 *
 * Allocates resources for a new coroutine,
 * and prepares it to run `start`.
 *
 * `start` will only be queued -
 * it won't actually be called
 * until the first call to \ref green_resume.
 *
 * \param[in] start     The entrypoint of the coroutine.
 * \param[in] arguments A value to be passed straight through to `start`.
 * \param[in] hint      A hint as to how big the stack may be.
 *                      The actual stack size may be larger,
 *                      and may be dynamic if the system supports it.
 *                      The stack will never start smaller than this value.
 *                      If zero, a sensible default will be used instead (16K).
 * \returns
 *  The handle to the newly-created coroutine.
 *  If sufficient resources cannot be allocated,
 *  returns `NULL` instead,
 *  and the system's error code mechanism
 *  may contain more information
 *  (for Linux, see `mmap(3)`).
 */
green_thread_t green_spawn(green_start_t start, void *arguments, size_t hint);

/**
 * Run the coroutine until it needs to wait for something.
 *
 * If the coroutine has not yet started
 * (i.e. the thread has been freshly returned from \ref green_spawn),
 * `resume_with` is ignored and
 * `start(arguments)` is called on the coroutine's stack.
 * Otherwise, execution resumes on the coroutine's stack
 * by causing a pending call to \ref green_await
 * to return the value given in `resume_with`.
 *
 * When this function returns, either:
 *
 * 1. the coroutine has just called \ref green_await; or
 * 2. the coroutine's `start` has just returned.
 *
 * In the latter case, all internal resources
 * have already been freed by the time this function returns.
 * This includes the stack pages having been removed from memory.
 * As such, it is imperative that the coroutine
 * organises with its caller beforehand to make sure
 * any memory that needs to persist
 * has been copied somewhere else.
 *
 * \param[in] thread      Handle to the coroutine to resume.
 * \param[in] resume_with The value to be returned from \ref green_await.
 *                        This value is ignored if the coroutine
 *                        has not started yet.
 * \returns
 *  1. The value passed to \ref green_await;
 *  2. `NULL`, if the coroutine has finished; or
 *  3. `GREEN_RESUME_FAILED`, if the coroutine is currently running
 *     (has already started and is not currently calling \ref green_await).
 */
green_await_t green_resume(green_thread_t thread, green_resume_t resume_with);

/**
 * Pause the current coroutine and yield control to the caller.
 *
 * Execution returns to the stack of
 * whatever last called \ref green_resume.
 * That function returns the value passed in `wait_for`.
 *
 * When this function returns,
 * \ref green_resume has been called with this thread,
 * and the value passed to `resume_with` is returned from this function.
 *
 * Do not call this function with a `wait_for` of `NULL`,
 * as this value is used to signal to the caller of \ref green_resume
 * that the coroutine has finished and is all cleaned up.
 * If you pass `NULL`,
 * this coroutine will never be resumed
 * and the allocated resources will be leaked.
 * The special value of `NULL` may be used in the future
 * to cause this coroutine to destructively stop early,
 * however this is currently not implemented for any platform.
 *
 * \param[in] wait_for The value to be returned from \ref green_resume.
 * \returns
 *  1. The value passed to \ref green_resume in `resume_with`; or
 *  2. `GREEN_AWAIT_FAILED`, if called outside of any coroutine.
 */
green_resume_t green_await(green_await_t wait_for);


/** Special value indicating a bad call to \ref green_resume. */
#define GREEN_RESUME_FAILED     ((green_await_t)&green_resume)

/** Special value indicating a bad call to \ref green_await. */
#define GREEN_AWAIT_FAILED      ((green_resume_t)&green_await)


#ifdef __cplusplus
}
#endif

#endif // include guard
