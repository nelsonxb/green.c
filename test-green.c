#include "green.h"

#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>


enum test_result {
    FAIL = 0x00,
    PASS = 0x01,
};

enum test_flags {
    TF_NORMAL = 0x00,
    TF_SKIP = 0x10,
    TF_TODO = 0x20,
    TF_CRITICAL = 0x40,
};

#define DIAG_LINE_MAX   128
#define DIAG_LINE_COUNT 64

struct diagnostics {
    size_t diag_lines;
    char buffer[DIAG_LINE_COUNT][DIAG_LINE_MAX];
};

struct test {
    enum test_result (*func)();
    const char *name;
    const char *directive;
    enum test_flags flags;
};


#define DECLTEST(id, name) \
    static enum test_result _test_impl_##id (); \
    static struct test id = { _test_impl_##id, name, NULL, TF_NORMAL }

#define DECLTEST_CR(id, name) \
    static enum test_result _test_impl_##id (); \
    static struct test id = { _test_impl_##id, name, NULL, TF_CRITICAL }

#define DEFTEST(id) \
    static enum test_result _test_impl_##id ()

#define SKIP(id, why) \
    ((id.directive = "skip " why), (id.flags |= TF_SKIP))
#define TODO(id, why) \
    ((id.directive = "TODO " why), (id.flags |= TF_TODO))


DECLTEST_CR(test_thread_runs, "thread gets run");
DECLTEST_CR(test_await_pauses, "await pauses thread");


static void D(const char *fmt, ...);
static void D_write();


static __thread void *_root_stack_active;
static size_t _root_stack_size = 4096;

static void _root_stack_init()
{
    void *root_s;
#ifdef __x86_64__
    asm (
        "movq   %%rsp, %[root_s]\n"
        : [root_s] "=rm" (root_s)
    );
#endif
    _root_stack_active = root_s;
}

static void *_root_stack()
{
    return _root_stack_active;
}

int main()
{

    _root_stack_init();

    static const struct test *tests[] = {
        &test_thread_runs,
        &test_await_pauses,
    };
    int n_tests = sizeof(tests) / sizeof(struct test *);

    printf("1..%zu\n", n_tests);
    D_write();

    enum test_result result;
    char *ok;
    int passed = 0, failed = 0, skipped = 0, todo = 0, todo_bonus = 0;
    for (int i = 0; i < n_tests; i += 1) {
        if (tests[i]->flags & TF_SKIP) {
            printf("ok %d %s # %s\n", i+1, tests[i]->name, tests[i]->directive);
            skipped += 1;
            continue;
        }

        result = tests[i]->func();
        if (result == PASS) {
            ok = "ok";
            passed += 1;
        } else {
            ok = "not ok";
            failed += 1;
        }

        if (tests[i]->flags & TF_TODO) {
            todo += 1;
            if (result == PASS) {
                todo_bonus += 1;
            } else {
                failed -= 1;
            }
        }

        if (tests[i]->directive == NULL) {
            printf("%s %d %s\n", ok, i+1,
                tests[i]->name);
        } else {
            printf("%s %d %s # %s\n", ok, i+1,
                tests[i]->name, tests[i]->directive);
        }
        
        D_write();

        if (tests[i]->flags & TF_CRITICAL && result != PASS) {
            printf("Bail out! Needed that test to pass.");
        }
    }

    printf("# Ran %d tests:\n", passed + failed);
    printf("#   - %d passed\n", passed);
    if (failed)
        printf("#   - %d failed\n", failed);
    if (skipped)
        printf("#   - %d skipped\n", skipped);
    if (todo && todo_bonus)
        printf("#   - %d todo, %d of which passed anyway\n", todo, todo_bonus);
    else if (todo)
        printf("#   - %d todo\n", todo);

    return 0;
}


static struct diagnostics _diag = { 0 };

static void D(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(_diag.buffer[_diag.diag_lines++], DIAG_LINE_MAX, fmt, ap);
    va_end(ap);
}

static void D_write()
{
    for (size_t li = 0; li < _diag.diag_lines; li += 1) {
        printf("# %s\n", _diag.buffer[li]);
    }

    memset(&_diag, 0, sizeof(_diag));
}


static void __attribute__((used))
_BAIL_BUFR(void *stack, long badindex,
           unsigned long avalue, unsigned long xvalue)
{
    D_write();
    printf("Bail out! Stack (%p) broken at buffer[%ld] (%lx != %lx)\n",
           stack, badindex, avalue, xvalue);
    exit(81);
}

static void __attribute__((used))
_BAIL_STOR(void *stack, long badindex,
           unsigned long avalue, unsigned long xvalue)
{
    D_write();
    printf("Bail out! Stack (%p) broken at regs[%ld] (%lx != %lx)\n",
           stack, badindex, avalue, xvalue);
    exit(81);
}

static void __attribute__((used))
_BAIL_ACTV(void *stack, green_thread_t xthread)
{
    D_write();
    if (xthread == _root_stack()) {
        printf("Bail out! Stack (%p) is not within root stack range (%p-%zu)\n",
               stack, xthread, _root_stack_size);
    } else {
        printf("Bail out! Stack (%p) is not on current thread (%p)\n",
               stack, xthread);
    }
    exit(81);
}


green_thread_t green_spawn_sp(
    green_start_t start,
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




/*/ Actual test implementations /*/


struct gaio_await {
    int id;
};

struct gaio_resume {
    int id;
};

struct test_args {
    int did_run;
};

static void basic_start_run_once(void *arguments)
{
    struct test_args *args = arguments;
    args->did_run = 1;
}

DEFTEST(test_thread_runs)
{
    green_thread_t co;
    green_await_t awon;
    struct test_args args = {
        .did_run = 0,
    };

    co = green_spawn_sp(basic_start_run_once, &args, 0);

    if (co == NULL) {
        D("thread not created (errno: %s)", strerror(errno));
        return FAIL;
    }
    else
    if (args.did_run == 1) {
        D("thread ran too early");
        return FAIL;
    }
    
    awon = green_resume_sp(co, NULL);
    if (awon == GREEN_RESUME_FAILED) {
        D("resume failed");
        return FAIL;
    } else if (awon != NULL) {
        D("thread awaited");
        return FAIL;
    }
    else
    if (args.did_run != 1) {
        D("did_run was %d (expect 1)", args.did_run);
        return FAIL;
    }

    return PASS;
}


static void basic_start_await(void *arguments)
{
    struct test_args *args = arguments;
    args->did_run = 0x0cfbbead;
    struct gaio_await await_on = { .id = args->did_run };
    
    green_resume_t res = green_await_sp(&await_on);
    if (res == GREEN_AWAIT_FAILED || res == NULL) {
        return;
    }

    args->did_run = res->id;
}

DEFTEST(test_await_pauses)
{
    green_thread_t co;
    green_await_t awon;
    struct test_args args = {
        .did_run = 0,
    };

    co = green_spawn_sp(basic_start_await, &args, 0);
    if (co == NULL) {
        D("thread not created: %s", strerror(errno));
        return FAIL;
    }

    awon = green_resume_sp(co, NULL);
    if (awon == GREEN_RESUME_FAILED) {
        D("resume failed");
        return FAIL;
    } else if (awon == NULL || args.did_run == 0) {
        D("thread returned early");
        return FAIL;
    } else if (awon->id != args.did_run) {
        D("awaited object id did not match");
        return FAIL;
    }

    int xid = awon->id + 1;
    struct gaio_resume resume = { .id = xid };
    awon = green_resume_sp(co, &resume);
    if (awon != NULL) {
        D("thread awaited");
        return FAIL;
    }
    else
    if (args.did_run != xid) {
        D("did_run was %d (expect %d)", args.did_run, xid);
        return FAIL;
    }

    return PASS;
}


#ifdef __x86_64__
    asm(
        "   .text                   \n"

        ".macro _sprot              \n"
#ifdef _GREEN_EXPORT_INTERNALS
        "   call    _green_current  \n"
        "   movq    (%rax), %rax    \n"
        "   cmpq    $0, %rax        \n"
        "   jne     1f              \n"
        "   call    _root_stack     \n"
        "   movq    _root_stack_size(%rip), %rcx    \n"
        "   jmp     2f              \n"
        "1:                         \n"
        "   movq    -40(%rax), %rcx \n"
        "2:                         \n"
        "   cmpq    %rax, %rsp      \n"
        "   jge     2f              \n"
        "   subq    %rcx, %rax      \n"
        "   cmpq    %rax, %rsp      \n"
        "   jge     1f              \n"
        "   addq    %rcx, %rax      \n"
        "2:                         \n"
        "   movq    %rsp, %rdi      \n"
        "   movq    %rax, %rsi      \n"
        "   call    _BAIL_ACTV      \n"
        "1:                         \n"
#endif
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
        "   movq    %rsp, %rdi      \n"
        "   addq    $32, %rdi       \n"
        "   subq    %rcx, %rdi      \n"
        "   movq    %rcx, %rsi      \n"
        "   movq    %rbp, %rcx      \n"
        "   call    _BAIL_BUFR      \n"
        "1:                         \n"
        "   movq    $6, %rsi        \n"
        "   popq    %rdx            \n"
        "   subq    $1, %rsi        \n"
        "   movq    %r15, %rcx      \n"
        "   cmpq    %rdx, %rcx      \n"
        "   jne     2f              \n"
        "   popq    %rdx            \n"
        "   subq    $1, %rsi        \n"
        "   movq    %r14, %rcx      \n"
        "   cmpq    %rdx, %rcx      \n"
        "   jne     2f              \n"
        "   popq    %rdx            \n"
        "   subq    $1, %rsi        \n"
        "   movq    %r13, %rcx      \n"
        "   cmpq    %rdx, %rcx      \n"
        "   jne     2f              \n"
        "   popq    %rdx            \n"
        "   subq    $1, %rsi        \n"
        "   movq    %r12, %rcx      \n"
        "   cmpq    %rdx, %rcx      \n"
        "   jne     2f              \n"
        "   popq    %rdx            \n"
        "   subq    $1, %rsi        \n"
        "   movq    %rbx, %rcx      \n"
        "   cmpq    %rdx, %rcx      \n"
        "   jne     2f              \n"
        "   popq    %rdx            \n"
        "   subq    $1, %rsi        \n"
        "   movq    %rbp, %rcx      \n"
        "   cmpq    %rdx, %rcx      \n"
        "   je      1f              \n"
        "2:                         \n"
        "   movq    %rsp, %rdi      \n"
        "   addq    $6, %rdi        \n"
        "   subq    %rdx, %rdi      \n"
        "   call    _BAIL_STOR      \n"
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
