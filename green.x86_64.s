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

	.globl green_spawn
	.globl green_resume
	.globl green_await

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
	addq	$24, %rsp

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
	# _green_thread_deactivate()
	call	_green_thread_deactivate
	# NOTE: deactivate should always return %rsi + 56,
	#       because if a thread is returning it must be active...
	#       right?

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
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rbx
	popq	%rbp
	ret


# green_await_t green_resume(green_thread_t thread, green_resume_t resume_with);
green_resume:
	# Save necessary registers
	pushq	%rbp
	pushq	%rbx
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15

	# thread(%rax) = _green_thread_activate(thread(%rdi))
	pushq	%rsi
	call	_green_thread_activate
	popq	%rsi

	cmpq	$0, %rax
	jne	_resume_activate_ok
	# activate returned NULL - that thread is already running!
	leaq	green_resume(%rip), %rax
	addq	$48, %rsp
	ret

_resume_activate_ok:
	# Swap stack pointers in thread->rsp
	movq	-16(%rax), %rdi
	movq	%rsp, -16(%rax)
	# Hop into thread stack
	movq	%rdi, %rsp
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
	# Save necessary registers
	pushq	%rbp
	pushq	%rbx
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15

	# thread = _green_thread_deactivate()
	pushq	%rdi
	call	_green_thread_deactivate
	popq	%rdi

	cmpq	$0, %rax
	jne	_await_deactivate_ok
	# deactivate returned NULL - we're not in a green thread!
	leaq	green_await(%rip), %rax
	addq	$48, %rsp
	ret

_await_deactivate_ok:
	# Prepare to return wait_for in %rax
	# but also need to keep thread in %rdi
	xchgq	%rax, %rdi

	# Swap stack pointers in thread->rsp
	movq	-16(%rdi), %rsi
	movq	%rsp, -16(%rdi)
	# Hop back to calling stack
	movq	%rsi, %rsp

	# restore saved registers and return
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%rbx
	popq	%rbp
	ret
