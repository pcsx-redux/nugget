// Built here rather than from common/libc so that the object, which carries
// openbios's own defines, does not land in the directory psyqo and the tests
// also compile it into.
#include "common/libc/xprintf.c"
