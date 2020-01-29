#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>
#include "green.h"


#define PASS(D)         printf("ok %d %s\n", ++*n, (D))
#define FAIL(D)         printf("not ok %d %s\n", ++*n, (D))
#define SKIP(D, W)      printf("ok %d %s # skip %s\n", ++*n, (D), (W))
#define TODO(D, W)      printf("not ok %d %s # TODO %s\n", ++*n, (D), (W))
#define TODO_BONUS(D, W)    printf("ok %d %s # TODO %s\n", ++*n, (D), (W))
#define ASSERT(C, D)            ((C) ? (PASS(D), 1) : (FAIL(D), 0))
#define ASSERT_SKIP(C, D, W)    (SKIP(D, W), 0)
#define ASSERT_TODO(C, D, W)    ((C) ? (TODO_BONUS(D), 1) : (TODO(D), 0))
#define DIAG(F, ...)    printf("# " F "\n", ##__VA_ARGS__)
#define BAIL(F, ...)    printf("Bail out! " F "\n", ##__VA_ARGS__); exit(81)
#define BAILAT(F, ...)  printf("Bail out! %s() [%s:%d]: " F "\n", \
                               __func__, __FILE__, __LINE__, \
                               ##__VA_ARGS__); \
                        exit(81)


static void test_basic(int *n);

int main()
{
    typedef void (*test_t)(int *n);
    static test_t tests[] = {
        test_basic
    };
    size_t n_tests = sizeof(tests) / sizeof(test_t);

    printf("TAP version 13\n");

    int n = 0;
    for (size_t i = 0; i < n_tests; i += 1) {
        tests[i](&n);
    }

    printf("1..%d\n", n);
    return 0;

stop:
    return 1;
}


struct gaio_await {
    int id;
};

struct gaio_resume {
    int id;
};


static const char *_BAIL_SP_BUFR = \
    "Bail out! Stack (%lx) broken at buffer[%ld] (%lx != %lx)\n";
static const char *_BAIL_SP_STOR = \
    "Bail out! Stack (%lx) broken at regs[%ld] (%lx != %lx)\n";
green_thread_t green_spawn_sp(
    void (*start)(void *arguments),
    void *arguments,
    size_t hint
) asm("_test_green__spawn_sp");
green_resume_t green_await_sp(
    green_await_t wait_for
) asm("_test_green__await_sp");
green_await_t green_resume_sp(
    green_thread_t thread,
    green_resume_t resume_with
) asm("_test_green__resume_sp");


/*/ test_basic /*/

struct basic_args {
    int did_run;
};

static void basic_start_run_once(void *arguments)
{
    struct basic_args *args = arguments;
    args->did_run = 1;
}

static void basic_start_await(void *arguments)
{
    struct basic_args *args = arguments;
    args->did_run = 0x0cfbbead;
    struct gaio_await await_on = { .id = args->did_run };
    
    green_resume_t res = green_await_sp(&await_on);
    if (res == GREEN_AWAIT_FAILED || res == NULL) {
        return;
    }

    args->did_run = res->id;
}

static void test_basic(int *n)
{
    const char *test;
    green_thread_t co;
    green_await_t awon;

    test = "thread gets run";
    struct basic_args args = {
        .did_run = 0,
    };

    co = green_spawn_sp(basic_start_run_once, &args, 0);

    if (co == NULL) {
        FAIL(test);
        DIAG("thread not created (errno: %s)", strerror(errno));
        BAIL("Basic thread operations not working.");
    }
    else
    if (args.did_run == 1) {
        FAIL(test);
        DIAG("thread ran too early");
        BAIL("Basic thread operations not working.");
    }
    
    awon = green_resume_sp(co, NULL);
    if (awon == GREEN_RESUME_FAILED) {
        FAIL(test);
        DIAG("resume failed");
        BAIL("Basic thread operations not working.");
    } else if (awon != NULL) {
        FAIL(test);
        DIAG("thread awaited");
        BAIL("Basic thread operations not working.");
    }
    else
    if (!ASSERT(args.did_run == 1, test)) {
        DIAG("did_run was %d (expect 1)", args.did_run);
        BAIL("Basic thread operations not working.");
    }


    test = "yield pauses thread";
    args.did_run = 0;

    co = green_spawn_sp(basic_start_await, &args, 0);
    if (co == NULL) {
        FAIL(test);
        DIAG("thread not created: %s");
        BAIL("Basic thread operations not working.");
    }

    awon = green_resume_sp(co, NULL);
    if (awon == GREEN_RESUME_FAILED) {
        FAIL(test);
        DIAG("resume failed");
        BAIL("Basic thread operations not working.");
    } else if (awon == NULL || args.did_run == 0) {
        FAIL(test);
        DIAG("thread returned early");
        BAIL("Basic thread operations not working.");
    } else if (awon->id != args.did_run) {
        FAIL(test);
        DIAG("awaited object id did not match");
        BAIL("Basic thread operations not working.");
    }

    int xid = awon->id + 1;
    struct gaio_resume resume = { .id = xid };
    awon = green_resume_sp(co, &resume);
    if (awon != NULL) {
        FAIL(test);
        DIAG("thread awaited");
        BAIL("Basic thread operations not working.");
    }
    else
    if (!ASSERT(args.did_run == xid, test)) {
        DIAG("did_run was %d (expect %d)", args.did_run, xid);
        BAIL("Basic thread operations not working.");
    }
}


#ifdef __x86_64__
    asm(
        "   .text                   \n"

        ".macro _sprot              \n"
        "   pushq   %rbp            \n"
        "   pushq   %rbx            \n"
        "   pushq   %r12            \n"
        "   pushq   %r13            \n"
        "   pushq   %r14            \n"
        "   pushq   %r15            \n"
        "   movq    $32, %rcx       \n"
        "1:                         \n"
        "   pushq   %rbp            \n"
        "   loop    1b              \n"
        ".endm                      \n"

        ".macro _stest              \n"
        "   movq    $32, %rcx       \n"
        "1:                         \n"
        "   popq    %rdx            \n"
        "   cmpq    %rdx, %rbp      \n"
        "   loope   1b              \n"
        "   je      1f              \n"
        "   movq    _BAIL_SP_BUFR(%rip), %rdi   \n"
        "   movq    %rsp, %rsi      \n"
        "   addq    $32, %rsi       \n"
        "   subq    %rcx, %rsi      \n"
        "   xchgq   %rcx, %rdx      \n"
        "   movq    %rbp, %r8       \n"
        "   movl    $0, %eax        \n"
        "   call    printf          \n"
        "   movl    $81, %edi       \n"
        "   call    exit            \n"
        "1:                         \n"
        "   movq    $6, %rdx        \n"
        "   popq    %rcx            \n"
        "   subq    $1, %rdx        \n"
        "   movq    %r15, %r8       \n"
        "   cmpq    %rcx, %r8       \n"
        "   jne     2f              \n"
        "   popq    %rcx            \n"
        "   subq    $1, %rdx        \n"
        "   movq    %r14, %r8       \n"
        "   cmpq    %rcx, %r8       \n"
        "   jne     2f              \n"
        "   popq    %rcx            \n"
        "   subq    $1, %rdx        \n"
        "   movq    %r13, %r8       \n"
        "   cmpq    %rcx, %r8       \n"
        "   jne     2f              \n"
        "   popq    %rcx            \n"
        "   subq    $1, %rdx        \n"
        "   movq    %r12, %r8       \n"
        "   cmpq    %rcx, %r8       \n"
        "   jne     2f              \n"
        "   popq    %rcx            \n"
        "   subq    $1, %rdx        \n"
        "   movq    %rbx, %r8       \n"
        "   cmpq    %rcx, %r8       \n"
        "   jne     2f              \n"
        "   popq    %rcx            \n"
        "   subq    $1, %rdx        \n"
        "   movq    %rbp, %r8       \n"
        "   cmpq    %rcx, %r8       \n"
        "   je      1f              \n"
        "2:                         \n"
        "   movq    _BAIL_SP_STOR(%rip), %rdi   \n"
        "   movq    %rsp, %rsi      \n"
        "   addq    $6, %rsi        \n"
        "   subq    %rdx, %rsi      \n"
        "   movl    $0, %eax        \n"
        "   call    printf          \n"
        "   movl    $81, %edi       \n"
        "   call    exit            \n"
        "1:                         \n"
        ".endm                      \n"

        "_test_green__spawn_sp:     \n"
        "   pushq   %r12            \n"
        "   pushq   %r13            \n"
        "   pushq   %r14            \n"
        "   pushq   %r15            \n"
        "   leaq    _test_green__spawn_sp(%rip), %r15   \n"
        "   movq    %rdi, %r14      \n"
        "   movq    %rsi, %r13      \n"
        "   movq    %rdx, %r12      \n"
        "   _sprot                  \n"
        "   call    green_spawn     \n"
        "   _stest                  \n"
        "   popq    %r15            \n"
        "   popq    %r14            \n"
        "   popq    %r13            \n"
        "   popq    %r12            \n"
        "   ret                     \n"

        "_test_green__await_sp:     \n"
        "   pushq   %r14            \n"
        "   pushq   %r15            \n"
        "   leaq    _test_green__await_sp(%rip), %r15   \n"
        "   movq    %rdi, %r14      \n"
        "   _sprot                  \n"
        "   call    green_await     \n"
        "   _stest                  \n"
        "   popq    %r15            \n"
        "   popq    %r14            \n"
        "   ret                     \n"

        "_test_green__resume_sp:    \n"
        "   pushq   %r13            \n"
        "   pushq   %r14            \n"
        "   pushq   %r15            \n"
        "   leaq    _test_green__resume_sp(%rip), %r15  \n"
        "   movq    %rdi, %r14      \n"
        "   movq    %rsi, %r13      \n"
        "   _sprot                  \n"
        "   call    green_resume    \n"
        "   _stest                  \n"
        "   popq    %r15            \n"
        "   popq    %r14            \n"
        "   popq    %r13            \n"
        "   ret                     \n"
    );
#endif
