Tiny coroutine library for C
============================

![CI](https://github.com/NelsonCrosby/green.c/workflows/tests/badge.svg?branch=master)

Green is a very small implementation of coroutines for C-compatible languages.
It is intended to act as a base for a green thread system.

Green's low-overhead assembler implementation
follows an _implicit yield_ pattern
(much like Lua's coroutines).
This is designed to make it easier to write code
using familiar synchronous patterns,
and should remain largely compatible with existing synchronous code.

Green currently only supports Linux on x86_64 and AArch64.
Other platforms may happen in the future.


Using
-----

Drop `green.c` and `green.*.s` somewhere in your source tree,
and add `green.c` to your build system
(the correct `green.*.s` will be included automatically from `green.c`).
Then drop `green.h` somewhere that `green.c` and
the rest of your code can find it,
read the header for documentation,
and you're away free.

Note that, as of right now, only GCC has been tested.

If you wanna run the test cases, simply run `./b.sh`.
Try passing `-q` if you wanna pipe it into some other test harness.
You could also pass `-t target` to try building for another platform
(this will not run the tests, because that probably isn't going to work,
 but you can copy the output to another device for testing).
