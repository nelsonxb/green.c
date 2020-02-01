# This file is part of https://github.com/NelsonCrosby/green.c

# BSD 2-Clause License
#
# Copyright (c) 2020, Nelson Crosby <nelson@tika.to>
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
# * Redistributions of source code must retain the above copyright notice, this
#   list of conditions and the following disclaimer.
#
# * Redistributions in binary form must reproduce the above copyright notice,
#   this list of conditions and the following disclaimer in the documentation
#   and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
# FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

	.text

	.globl	green_spawn
	.globl	green_resume
	.globl	green_await

# green_thread_t green_spawn(green_start_t start, void *arguments, size_t hint);
green_spawn:
	stp	x29, lr, [sp, #-16]!
	mov	x29, sp
	stp	x0, x1, [sp, #-16]!

	# if (hint == 0) hint = 16K
	# and make it the second argument to mmap
	adds	x1, x2, #0
	mov	x2, #0x4000
	csel	x1, x1, x2, ne
	str	x1, [sp, #-16]!

	# mmap(NULL, hint, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0)
	mov	x0, #0
	mov	w2, #0x03
	mov	w3, #0x22
	mov	w4, #-1
	mov	x5, #0
	bl	mmap

	cmp	x0, #-1
	bne	_alloc_ok
	# mmap returned MAP_FAILED; gotta return NULL.
	mov	x0, #0
	mov	sp, x29
	ldp	x29, lr, [sp], #16
	ret

_alloc_ok:
	# Get back arguments we set aside;
	# shift handle up to top of thread stack, minus header
	ldr	x1, [sp], #16
	ldp	x2, x3, [sp], #16
	add	x0, x0, x1
	sub	x0, x0, #48

	# Prepare stack header (C layout) {
	#   green_thread_t last_active;                     // 0
	#   size_t alloc_length;                            // 8
	#   void (*start)(void *arguments);                 // 16
	#   void *arguments;                                // 24
	#   union { void *calling; void *resuming; } sp;    // 32
	#   long padding;                                   // 40
	# }                                            // size 48
	stp	x0, x1, [x0, #0]
	stp	x2, x3, [x0, #16]

        # Prepare stack for resume to return into _thread_call
	mov	x4, x0
	# fp, lr
	mov	x1, x0
	adr	x2, _thread_call
	stp	x1, x2, [x4, #-16]!
	# Allocate space for another 12(!) saved registers
	sub	x4, x0, #96
	# Save stack pointer
	str	x4, [x0, #32]

	# Return (note: thread handle is at the low address of the header!)
	ldp	x29, lr, [sp], #16
	ret

_thread_call:
	# thread->start(arguments)
	ldp	x1, x0, [sp, #16]
	blr	x1

_thread_return:
	bl	_green_current
	# NOTE: *current should always be sp,
	#       and the active flag should always be set,
	#       because if a thread is returning it must be active...
	#       right?

	# Restore last active thread
	ldr	x1, [sp]
	str	x1, [x0]

	# Prepare munmap arguments
	add	x0, sp, #48
	ldr	x1, [sp, #8]
	# Hop back to calling stack
	ldr	x2, [sp, #32]
	mov	sp, x2

	# Resolve bottom of allocation and call munmap
	sub	x0, x0, x1
	bl	munmap

	# Restore saved registers and return
	# (uses zero returned from munmap)
        ldp	x19, x20, [sp], #16
        ldp	x21, x22, [sp], #16
        ldp	x23, x24, [sp], #16
        ldp	x25, x26, [sp], #16
        ldp	x27, x28, [sp], #16
        ldp	x29, lr,  [sp], #16
	ret


# green_await_t green_resume(green_thread_t thread, green_resume_t resume_with);
green_resume:
	stp	x29, lr, [sp, #-16]!
	mov	x29, sp

	# Get thread-local current thread (green_thread_t *)
	stp	x0, x1, [sp, #-16]!
	bl	_green_current
	ldp	x2, x1, [sp], #16
	ldr	x4, [x0]

	# Try to activate thread
	ldaxr	x3, [x2]
	cmp	x3, x2
	bne	_resume_activate_fail
	stlxr	w5, x4, [x2]
	cbz	w5, _resume_activate_ok

_resume_activate_fail:
	# Thread could not be activated - it's already running somewhere!
	adr	x0, green_resume
	ldp	x29, lr, [sp], #16
	ret

_resume_activate_ok:
	# Set thread as current
	str	x2, [x0]

	# Save necessary registers
	# fp, lr already saved at function entry
        stp	x27, x28, [sp, #-16]!
        stp	x25, x26, [sp, #-16]!
        stp	x23, x24, [sp, #-16]!
        stp	x21, x22, [sp, #-16]!
        stp	x19, x20, [sp, #-16]!

	# Swap stack pointers in thread->sp
	mov	x4, sp
	ldr	x3, [x2, #32]
	str	x4, [x2, #32]
	# Hop into thread stack
	mov	sp, x3
	# return resume_with
	mov	x0, x1

	# restore saved registers and return
        ldp	x19, x20, [sp], #16
        ldp	x21, x22, [sp], #16
        ldp	x23, x24, [sp], #16
        ldp	x25, x26, [sp], #16
        ldp	x27, x28, [sp], #16
        ldp	x29, lr,  [sp], #16
	ret


# green_resume_t green_await(green_await_t wait_for);
green_await:
	stp	x29, lr, [sp, #-16]!
	mov	x29, sp

	# Get thread-local current thread (green_thread_t *)
	str	x0, [sp, #-16]!
	bl	_green_current
	ldr	x1, [sp], #16
	ldr	x2, [x0]

	# Note that this whole process is non-atomic, since:
	# 1. No two systhreads can share gthread in the *current list
	#    (so only this systhread can call await); and
	# 2. No systhread can resume this gthread until it is completely deactivated
	#    (as the very last operation of this function).
	# Therefore, this gthread is safely locked to this systhread
	# until the whole function completes.

	# Ensure there is actually a thread running
	cbnz	x2, _await_ok
	# There is not! We're being called from a root stack!
	adr	x0, green_await
	ldp	x29, lr, [sp], #16
	ret

_await_ok:
	# Save necessary registers
	# fp, lr already saved at function entry
        stp	x27, x28, [sp, #-16]!
        stp	x25, x26, [sp, #-16]!
        stp	x23, x24, [sp, #-16]!
        stp	x21, x22, [sp, #-16]!
        stp	x19, x20, [sp, #-16]!

	# Swap stack pointers in thread->sp
	mov	x4, sp
	ldr	x3, [x2, #32]
	str	x4, [x2, #32]
	# Hop back to calling stack
	mov	sp, x3

	# restore saved registers
        ldp	x19, x20, [sp], #16
        ldp	x21, x22, [sp], #16
        ldp	x23, x24, [sp], #16
        ldp	x25, x26, [sp], #16
        ldp	x27, x28, [sp], #16

	# *current = last_active
	ldr	x3, [x2]
	str	x3, [x0]
	# Deactivate thread
	str	x2, [x2]

	# return wait_for
	mov	x0, x1
        ldp	x29, lr,  [sp], #16
	ret
