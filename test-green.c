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
#define BAIL(F, ...)    printf("Bail out! " F "\n", ##__VA_ARGS__); return TEST_ABORT


enum test_result { TEST_OK, TEST_ABORT };

static enum test_result test_basic(int *n);

int main()
{
    typedef enum test_result (*test_t)(int *n);
    static test_t tests[] = {
        test_basic
    };
    size_t n_tests = sizeof(tests) / sizeof(test_t);

    printf("TAP version 13\n");

    int n = 0;
    for (size_t i = 0; i < n_tests; i += 1) {
        switch (tests[i](&n)) {
        case TEST_OK:
            continue;
        case TEST_ABORT:
            goto stop;
        }
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
    
    green_resume_t res = green_await(&await_on);
    if (res == GREEN_AWAIT_FAILED || res == NULL) {
        return;
    }

    args->did_run = res->id;
}

static enum test_result test_basic(int *n)
{
    const char *test;

    test = "thread gets run";
    struct basic_args args = {
        .did_run = 0,
    };
    green_thread_t co = green_spawn(basic_start_run_once, &args, 0);

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
    
    green_await_t result = green_resume(co, NULL);
    if (result == GREEN_RESUME_FAILED) {
        FAIL(test);
        DIAG("resume failed");
        BAIL("Basic thread operations not working.");
    } else if (result != NULL) {
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
    co = green_spawn(basic_start_await, &args, 0);

    if (co == NULL) {
        FAIL(test);
        DIAG("thread not created: %s");
        BAIL("Basic thread operations not working.");
    }

    green_await_t awaited = green_resume(co, NULL);
    if (awaited == GREEN_RESUME_FAILED) {
        FAIL(test);
        DIAG("resume failed");
        BAIL("Basic thread operations not working.");
    } else if (awaited == NULL || args.did_run == 0) {
        FAIL(test);
        DIAG("thread returned early");
        BAIL("Basic thread operations not working.");
    } else if (awaited->id != args.did_run) {
        FAIL(test);
        DIAG("awaited object id did not match");
        BAIL("Basic thread operations not working.");
    }

    int xid = awaited->id + 1;
    struct gaio_resume resume = { .id = xid };
    awaited = green_resume(co, &resume);
    if (awaited != NULL) {
        FAIL(test);
        DIAG("thread awaited");
        BAIL("Basic thread operations not working.");
    }
    else
    if (!ASSERT(args.did_run == xid, test)) {
        DIAG("did_run was %d (expect %d)", args.did_run, xid);
        BAIL("Basic thread operations not working.");
    }


    return TEST_OK;
}
