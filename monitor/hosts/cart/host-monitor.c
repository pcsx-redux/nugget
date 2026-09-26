/* Compiled here rather than as ../../monitor.c so this host gets its own object:
   the monitor sources build under a different link per host, and a shared
   object in monitor/ is whichever build ran last. */
#include "monitor/monitor.c"
