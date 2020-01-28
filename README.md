Tiny coroutine library for C
============================

Green is a very small implementation of coroutines for C-compatible languages.
It is intended to act as a base for a green thread system.

Green's low-overhead assembler implementation
follows an _implicit yield_ pattern
(much like Lua's coroutines).
This should make sure it remains compatible
with all sorts of existing synchronous algorithms.

Green currently only supports Linux on x86_64.
AArch64 support is probably coming soon,
maybe also 32-bit ARM.
Other OSes may be implemented as I need them
(Windows support is likely to happen at some point).


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
