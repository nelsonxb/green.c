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


static void D(const char *fmt, ...);
static void D_write();


static __thread void *_root_stack_active;
static size_t __attribute__((used)) _root_stack_size = 4096;

static void _root_stack_init()
{
    void *root_s;
#if defined(__x86_64__)
    asm (
        "movq   %%rsp, %[root_s]\n"
        : [root_s] "=rm" (root_s)
    );

#elif defined(__aarch64__)
    asm (
        "mov    %[root_s], sp\n"
        : [root_s] "=r" (root_s)
    );

#endif
    _root_stack_active = root_s;
}

static void *__attribute__((used)) _root_stack()
{
    return _root_stack_active;
}


DECLTEST_CR(test_thread_runs, "coroutine gets run");
DECLTEST_CR(test_await_pauses, "await pauses coroutine");
DECLTEST(test_thread_switches, "multiple coroutines switch without interfering");
DECLTEST(test_thread_nesting, "coroutines can start and resume each other");

DECLTEST(test_bad_alloc, "spawn returns a sensible error when allocation was not possible");
DECLTEST(test_bad_resume, "coroutine cannot resume while already running");
DECLTEST(test_bad_await, "cannot await from outside a coroutine");

int main()
{

    _root_stack_init();

    static const struct test *tests[] = {
        &test_thread_runs,
        &test_await_pauses,
        &test_thread_switches,
        &test_thread_nesting,
        &test_bad_alloc,
        &test_bad_resume,
        &test_bad_await,
    };
    int n_tests = sizeof(tests) / sizeof(struct test *);

    printf("1..%d\n", n_tests);
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
            printf("Bail out! Needed that test to pass.\n");
            return 1;
        }
    }

    printf("#\n");
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
    printf("#\n");

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
    green_thread_t subthread;
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
    if (res == GREEN_AWAIT_FAILED) {
        D("await failed");
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


static void schedtest_start(void *arguments)
{
    struct test_args *args = arguments;
    struct gaio_await awon = { 0 };
    do {
        awon.id += 1;
    } while (green_await_sp(&awon) != NULL);
    args->did_run = awon.id;
}

DEFTEST(test_thread_switches)
{
    green_thread_t co[6];
    struct test_args arguments[6];
    green_await_t awon;

    for (size_t i = 0; i < 6; i += 1) {
        arguments[i].did_run = 0;
        co[i] = green_spawn_sp(schedtest_start, &arguments[i], 4096);
        if (co[i] == NULL) {
            D("thread %d not created: %s", i, strerror(errno));
            return FAIL;
        }
    }

    struct gaio_resume resinfo = {};
    for (size_t i = 0; i < 6; i += 1)
    for (size_t k = i; k < 6; k += 1) {
        awon = green_resume_sp(co[k], &resinfo);
        if (awon == GREEN_RESUME_FAILED) {
            D("resume thread %zu (round %zu) failed", k, i);
            return FAIL;
        } else if (awon == NULL) {
            D("thread %zu (round %zu) returned early", k, i);
            return FAIL;
        }
    }

    for (size_t i = 0; i < 6; i += 1) {
        awon = green_resume_sp(co[i], NULL);
        if (awon == GREEN_RESUME_FAILED) {
            D("resume thread %zu (stopping) failed", i);
            return FAIL;
        } else if (awon != NULL) {
            D("thread %zu failed to return", i);
            return FAIL;
        }
    }

    size_t nne = 0;
    for (int i = 0; i < 6; i += 1) {
        if (arguments[i].did_run != (i + 1)) {
            nne += 1;
            D("thread %d gave incorrect count %d", i, arguments[i].did_run);
        }
    }

    return nne > 0 ? FAIL : PASS;
}


struct nesttest_args {
    green_thread_t parent;
};

static void nesttest_start_b(void *arguments)
{
    green_thread_t a = ((struct nesttest_args *)arguments)->parent;
    green_await_t awon;
    green_resume_t next;

    struct gaio_await b_awon = { 2 };
    next = green_await_sp(&b_awon);
    if (next == GREEN_AWAIT_FAILED) {
        D("await failed in b");
        return;
    } else if (next->id != 1) {
        D("b expected to be resumed by a, got %d", next->id);
        return;
    }

    next = green_await_sp(&b_awon);
    if (next == GREEN_AWAIT_FAILED) {
        D("await failed in b");
        return;
    } else if (next->id != 0) {
        D("b expected to be resumed by root, got %d", next->id);
        return;
    }

    struct gaio_resume b_resinfo = { 2 };
    awon = green_resume_sp(a, &b_resinfo);
    if (awon == GREEN_RESUME_FAILED) {
        D("resume failed (b calls a)");
        return;
    } else if (awon == NULL) {
        D("a returned early (b calls a)");
        return;
    } else if (awon->id != 1) {
        D("b expected that a would await 1, got %d", awon->id);
        return;
    }

    green_await_sp(&b_awon);
}

green_thread_t *_green_current();

static void nesttest_start_a(void *arguments)
{
    green_thread_t b;
    green_await_t awon;
    green_resume_t next;

    struct nesttest_args b_args = { *_green_current() };
    b = green_spawn_sp(nesttest_start_b, &b_args, 4096);
    if (b == NULL) {
        D("thread b not created: %s", strerror(errno));
        return;
    }

    awon = green_resume_sp(b, NULL);
    if (awon == GREEN_RESUME_FAILED) {
        D("failed to start b (a resumes b)");
        return;
    } else if (awon == NULL) {
        D("b returned immediately (a resumes b)");
        return;
    } else if (awon->id != 2) {
        D("expected that b would await 2, got %d", awon->id);
        return;
    }

    struct gaio_await a_awon = { 1, b };
    next = green_await_sp(&a_awon);
    if (next == GREEN_AWAIT_FAILED) {
        D("await failed in a");
        return;
    } else if (next->id != 0) {
        D("a expected to be resumed by root, got %d", next->id);
        return;
    }

    struct gaio_resume a_resinfo = { 1 };
    awon = green_resume_sp(b, &a_resinfo);
    if (awon == GREEN_RESUME_FAILED) {
        D("resume failed (a calls b)");
        return;
    } else if (awon == NULL) {
        D("b returned early (a calls b)");
        return;
    } else if (awon->id != 2) {
        D("a expected that b would await 2, got %d", awon->id);
        return;
    }

    next = green_await_sp(&a_awon);
    if (next == GREEN_AWAIT_FAILED) {
        D("await failed in a");
        return;
    } else if (next->id != 2) {
        D("a expected to be resumed by b, got %d", awon->id);
        return;
    }

    green_await_sp(&a_awon);
}

DEFTEST(test_thread_nesting)
{
    green_thread_t a, b;
    green_await_t awon;

    a = green_spawn_sp(nesttest_start_a, NULL, 4096);
    if (a == NULL) {
        D("thread a not created: %s", strerror(errno));
        return FAIL;
    }

    awon = green_resume_sp(a, NULL);
    if (awon == GREEN_RESUME_FAILED) {
        D("resume failed on a");
        return FAIL;
    } else if (awon == NULL) {
        D("a returned early");
        return FAIL;
    } else if ((b = awon->subthread) == NULL) {
        D("a did not send back a coroutine");
        return FAIL;
    }

    struct gaio_resume resinfo = { 0 };
    awon = green_resume_sp(a, &resinfo);
    if (awon == GREEN_RESUME_FAILED) {
        D("resume failed on a");
        return FAIL;
    } else if (awon == NULL) {
        D("a returned early");
        return FAIL;
    }

    awon = green_resume_sp(b, &resinfo);
    if (awon == GREEN_RESUME_FAILED) {
        D("resume failed on b");
        return FAIL;
    } else if (awon == NULL) {
        D("b returned early");
        return FAIL;
    }

    awon = green_resume_sp(a, NULL);
    if (awon == GREEN_RESUME_FAILED) {
        D("final resume failed on a");
        return FAIL;
    } else if (awon != NULL) {
        D("final resume did not end a");
        return FAIL;
    }

    awon = green_resume_sp(b, NULL);
    if (awon == GREEN_RESUME_FAILED) {
        D("final resume failed on b");
        return FAIL;
    } else if (awon != NULL) {
        D("final resume did not end b");
        return FAIL;
    }

    return PASS;
}


DEFTEST(test_bad_alloc)
{
    green_thread_t co = green_spawn_sp(basic_start_run_once, NULL, -1ULL);
    if (co != NULL) {
        D("somehow managed to allocate a thread of size %zu??", -1ULL);
        return FAIL;
    } else if (errno != ENOMEM) {
        D("errno should have been ENOMEM, got: %s", strerror(errno));
        return FAIL;
    }

    return PASS;
}

static void bad_resume_start(void *arguments)
{
    enum test_result *result = arguments;
    green_await_t awon = green_resume_sp(*_green_current(), NULL);
    if (awon != GREEN_RESUME_FAILED) {
        D("somehow managed to resume a running thread");
        *result = FAIL;
        return;
    }

    *result = PASS;
}

DEFTEST(test_bad_resume)
{
    green_thread_t co;
    green_await_t awon;
    enum test_result result = FAIL;

    co = green_spawn_sp(bad_resume_start, &result, 4096);
    if (co == NULL) {
        D("thread not created: %s", strerror(errno));
        return FAIL;
    }

    awon = green_resume_sp(co, NULL);
    if (awon == GREEN_RESUME_FAILED) {
        D("failed to start");
        return FAIL;
    } else if (awon != NULL) {
        D("unexpected await");
        return FAIL;
    }

    return result;
}

DEFTEST(test_bad_await)
{
    struct gaio_await awon = {};
    green_resume_t next = green_await_sp(&awon);
    if (next != GREEN_AWAIT_FAILED) {
        D("await was somehow successful??");
        return FAIL;
    }

    return PASS;
}


#if defined(__x86_64__)
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

#elif defined(__aarch64__)
    asm(
        "   .text                           \n"

        ".macro _sprot                      \n"
#ifdef _GREEN_EXPORT_INTERNALS
        "   bl      _green_current          \n"
        "   ldr     x0, [x0]                \n"
        "   cmp     x0, #0                  \n"
        "   bne     1f                      \n"
        "   bl      _root_stack             \n"
        "   ldr     x1, _root_stack_size    \n"
        "   b       2f                      \n"
        "1:                                 \n"
        "   ldr     x1, [x0, #8]            \n"
        "2:                                 \n"
        "   cmp     sp, x0                  \n"
        "   bge     2f                      \n"
        "   sub     x0, x0, x1              \n"
        "   cmp     sp, x0                  \n"
        "   bge     1f                      \n"
        "   add     x0, x0, x1              \n"
        "2:                                 \n"
        "   mov     x0, x1                  \n"
        "   mov     sp, x0                  \n"
        "   bl      _BAIL_ACTV              \n"
        "1:                                 \n"
#endif
        "   mov     x6, sp                  \n"
        "   stp     x29, x6, [sp, #-16]!    \n"
        "   stp     x27, x28, [sp, #-16]!   \n"
        "   stp     x25, x26, [sp, #-16]!   \n"
        "   stp     x23, x24, [sp, #-16]!   \n"
        "   stp     x21, x22, [sp, #-16]!   \n"
        "   stp     x19, x20, [sp, #-16]!   \n"
        "   sub     x1, sp, #(32*8)         \n"
        "1:                                 \n"
        "   stp     x29, x29, [sp, #-16]!   \n"
        "   cmp     sp, x1                  \n"
        "   bne     1b                      \n"
        ".endm                              \n"

        ".macro _stest                      \n"
        "   mov     x6, sp                  \n"
        "   mov     x1, #31                 \n"
        "   mov     x3, x29                 \n"
        "1:                                 \n"
        "   ldr     x2, [x6, x1, lsl #3]    \n"
        "   cmp     x2, x3                  \n"
        "   bne     2f                      \n"
        "   subs    x1, x1, #1              \n"
        "   bpl     1b                      \n"
        "   add     x6, x6, #(32*8)         \n"
        "   b       1f                      \n"
        "2:                                 \n"
        "   mov     x0, x6                  \n"
        "   mov     x5, #31                 \n"
        "   sub     x1, x5, x1              \n"
        "   mov     x3, x29                 \n"
        "   bl      _BAIL_BUFR              \n"
        "1:                                 \n"
        "   ldr     x2, [x6], #8            \n"
        "   cmp     x2, x19                 \n"
        "   csel    x3, x19, x3, ne         \n"
        "   bne     2f                      \n"
        "   ldr     x2, [x6], #8            \n"
        "   cmp     x2, x20                 \n"
        "   csel    x3, x20, x3, ne         \n"
        "   bne     2f                      \n"
        "   ldr     x2, [x6], #8            \n"
        "   cmp     x2, x21                 \n"
        "   csel    x3, x21, x3, ne         \n"
        "   bne     2f                      \n"
        "   ldr     x2, [x6], #8            \n"
        "   cmp     x2, x22                 \n"
        "   csel    x3, x22, x3, ne         \n"
        "   bne     2f                      \n"
        "   ldr     x2, [x6], #8            \n"
        "   cmp     x2, x23                 \n"
        "   csel    x3, x23, x3, ne         \n"
        "   bne     2f                      \n"
        "   ldr     x2, [x6], #8            \n"
        "   cmp     x2, x24                 \n"
        "   csel    x3, x24, x3, ne         \n"
        "   bne     2f                      \n"
        "   ldr     x2, [x6], #8            \n"
        "   cmp     x2, x25                 \n"
        "   csel    x3, x25, x3, ne         \n"
        "   bne     2f                      \n"
        "   ldr     x2, [x6], #8            \n"
        "   cmp     x2, x26                 \n"
        "   csel    x3, x26, x3, ne         \n"
        "   bne     2f                      \n"
        "   ldr     x2, [x6], #8            \n"
        "   cmp     x2, x27                 \n"
        "   csel    x3, x27, x3, ne         \n"
        "   bne     2f                      \n"
        "   ldr     x2, [x6], #8            \n"
        "   cmp     x2, x28                 \n"
        "   csel    x3, x28, x3, ne         \n"
        "   bne     2f                      \n"
        "   ldr     x2, [x6], #8            \n"
        "   cmp     x2, x29                 \n"
        "   csel    x3, x29, x3, ne         \n"
        "   bne     2f                      \n"
        "   ldr     x2, [x6], #8            \n"
        "   cmp     x2, x6                  \n"
        "   csel    x3, x6, x3, ne          \n"
        "   beq     1f                      \n"
        "2:                                 \n"
        "   mov     x5, sp                  \n"
        "   sub     x1, x6, x6              \n"
        "   add     x0, sp, #12             \n"
        "   bl      _BAIL_STOR              \n"
        "1:                                 \n"
        "   mov     sp, x6                  \n"
        ".endm                              \n"

        "_test_green__spawn_sp:             \n"
        "   stp     x29, lr, [sp, #-16]!    \n"
        "   mov     x29, sp                 \n"
        "   stp     x19, x20, [sp, #-16]!   \n"
        "   stp     x21, x22, [sp, #-16]!   \n"
        "   adr     x19, _test_green__spawn_sp  \n"
        "   mov     x20, x0                 \n"
        "   mov     x21, x1                 \n"
        "   mov     x22, x2                 \n"
        "   _sprot                          \n"
        "   mov     x0, x20                 \n"
        "   mov     x1, x21                 \n"
        "   mov     x2, x22                 \n"
        "   bl      green_spawn             \n"
        "   _stest                          \n"
        "   ldp     x21, x22, [sp], #16     \n"
        "   ldp     x19, x20, [sp], #16     \n"
        "   ldp     x29, lr, [sp], #16      \n"
        "   ret                             \n"

        "_test_green__await_sp:             \n"
        "   stp     x29, lr, [sp, #-16]!    \n"
        "   stp     x19, x20, [sp, #-16]!   \n"
        "   adr     x19, _test_green__await_sp  \n"
        "   mov     x20, x0                 \n"
        "   _sprot                          \n"
        "   mov     x0, x20                 \n"
        "   mov     x1, x21                 \n"
        "   bl      green_await             \n"
        "   _stest                          \n"
        "   ldp     x19, x20, [sp], #16     \n"
        "   ldp     x29, lr, [sp], #16      \n"
        "   ret                             \n"

        "_test_green__resume_sp:            \n"
        "   stp     x29, lr, [sp, #-16]!    \n"
        "   stp     x19, x20, [sp, #-16]!   \n"
        "   str     x21, [sp, #-16]!        \n"
        "   adr     x19, _test_green__resume_sp \n"
        "   mov     x20, x0                 \n"
        "   mov     x21, x1                 \n"
        "   _sprot                          \n"
        "   mov     x0, x20                 \n"
        "   mov     x1, x21                 \n"
        "   bl      green_resume            \n"
        "   _stest                          \n"
        "   ldr     x21, [sp], #16          \n"
        "   ldp     x19, x20, [sp], #16     \n"
        "   ldp     x29, lr, [sp], #16      \n"
        "   ret                             \n"
    );

#endif
