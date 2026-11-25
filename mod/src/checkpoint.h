/* Which level the checkpoint in `savegame.aws` belongs to. No platform dependencies. */
#ifndef CHECKPOINT_H
#define CHECKPOINT_H

#include <stddef.h>

/* Level number (1..17, 100, 200) from the first bytes of the file, or 0 if not found. */
int checkpoint_level(const unsigned char *data, size_t size);

#endif
