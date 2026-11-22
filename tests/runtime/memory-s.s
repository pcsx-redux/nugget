# Built here rather than from common/crt0 so that the object does not land
# in the directory psyqo and the shell also compile it into.
.include "common/crt0/memory-s.s"
