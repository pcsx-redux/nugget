// Built here rather than from common/crt0 so that the object does not land in
// the directory the shell, which openbios builds in parallel, also links from.
#include "common/crt0/memory-c.c"
