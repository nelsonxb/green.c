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
	enter	$24, $0
	# Save start, arguments for a second
	movq	%rdi, -8(%rbp)
	movq	%rsi, -16(%rbp)

	# if (hint == 0) hint = 16K
	# hint => %rsi (second argument to mmap)
	movq	%rdx, %rsi
	cmpq	$0, %rsi
	jne	_alloc
	movq	$0x4000, %rsi

_alloc:
	# Save length for a second
	movq	%rsi, -24(%rbp)
	# mmap(NULL, hint, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0)
	movq	$0, %rdi
	movl	$3, %edx
	movl	$0x22, %ecx
	movl	$-1, %r8d
	movq	$0, %r9
	call	mmap

	cmpq	$-1, %rax
	jne	_alloc_ok
	# mmap returned MAP_FAILED; gotta return NULL.
	movq	$0, %rax
	leave
	ret

_alloc_ok:
	# Get back values we set aside (start, arguments, length);
	# shift address up to top of thread stack
	movq	-8(%rbp), %rdx
	movq	-16(%rbp), %rcx
	movq	-24(%rbp), %rsi
	addq	%rsi, %rax

	# Prepare stack header (high address downward) {
	#   green_thread_t last_active;                         // -8
	#   union { void *calling; void *resuming; } rsp;       // -16
	#   void (*start)(void *arguments);                     // -24
	#   void *arguments;                                    // -32
	#   size_t alloc_length;                                // -40
	#   long padding;                                       // -48
	#   void *_thread_call_retptr;  // initial stack top    // -56
	# }
	leaq	-56(%rax), %rdi
	addq	$-48, %rdi
	movq	%rax, -8(%rax)
	movq	%rdi, -16(%rax)
	movq	%rdx, -24(%rax)
	movq	%rcx, -32(%rax)
	movq	%rsi, -40(%rax)

	leaq	_thread_call(%rip), %rsi
	movq	%rsi, 48(%rdi)

	leave
	ret

_thread_call:
	# thread->start(arguments)
	movq	16(%rsp), %rdi
	call	*24(%rsp)

_thread_return:
	call	_green_current
	# NOTE: *current should always be %rsi + 56,
	#	and -8(*current) should never be *current,
	#       because if a thread is returning it must be active...
	#       right?

	# Restore last active thread
	movq	(%rax), %rdi
	movq	-8(%rdi), %rdi
	movq	%rdi, (%rax)

	# Prepare munmap arguments
	leaq	48(%rsp), %rdi
	movq	-40(%rdi), %rsi
	# Hop back to calling stack
	movq	-16(%rdi), %rsp

	# resolve bottom of allocation
	subq	%rsi, %rdi
	# munmap(thread - thread->alloc_length, thread->alloc_length)
	call	munmap

	# restore saved registers and return
	# (uses zero returned from munmap)
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rbx
	popq	%rbp
	ret


# green_await_t green_resume(green_thread_t thread, green_resume_t resume_with);
green_resume:
	# Get thread-local current thread (green_thread_t *)
	pushq	%rsi
	pushq	%rdi
	call	_green_current
	popq	%rdi
	popq	%rsi

	movq	%rax, %r8
	# Try to activate thread
	movq	(%r8), %rcx
	movq	%rdi, %rax
lock	cmpxchg	%rcx, -8(%rdi)
	je	_resume_activate_ok
	# Thread could not be activated - it's already running somewhere!
	leaq	green_resume(%rip), %rax
	ret

_resume_activate_ok:
	# Set thread as current
	movq	%rdi, (%r8)

	# Save necessary registers
	pushq	%rbp
	pushq	%rbx
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15

	# Swap stack pointers in thread->rsp
	movq	-16(%rdi), %rdx
	movq	%rsp, -16(%rdi)
	# Hop into thread stack
	movq	%rdx, %rsp
	# return resume_with
	movq	%rsi, %rax

	# restore saved registers and return
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rbx
	popq	%rbp
	ret


# green_resume_t green_await(green_await_t wait_for);
green_await:
	# Get thread-local current thread (green_thread_t *)
	pushq	%rdi
	call	_green_current
	movq	%rax, %r8
	popq	%rax    # Put wait_for into %rax for returning later

	# Note that this whole process is non-atomic, since:
	# 1. No two systhreads can share gthread in the *current list
	#    (so only this systhread can call await); and
	# 2. No systhread can resume this gthread until it is completely deactivated
	#    (as the very last operation of this function).
	# Therefore, this gthread is safely locked to this systhread
	# until the whole function completes.

	# Ensure there is actually a thread running
	movq	(%r8), %rdi
	cmpq	$0, %rdi
	jne	_await_ok
	# There is not! We're being called from the root stack!
	leaq	green_await(%rip), %rax
	ret

_await_ok:
	# Save necessary registers
	pushq	%rbp
	pushq	%rbx
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15

	# Swap stack pointers in thread->rsp
	movq	-16(%rdi), %rsi
	movq	%rsp, -16(%rdi)
	# Hop back to calling stack
	movq	%rsi, %rsp

	# restore saved registers
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rbx
	popq	%rbp

	# *current = last_active
	movq	-8(%rdi), %rsi
	movq	%rsi, (%r8)
	# Deactivate thread
	movq	%rdi, -8(%rdi)

	ret
