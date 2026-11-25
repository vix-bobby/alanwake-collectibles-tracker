#include <string.h>

#include "checkpoint.h"

/* u32, u32, u32 length, then e.g. "gameworld:level_05" */
int checkpoint_level(const unsigned char *d, size_t size)
{
    size_t length, i, digits;
    int level = 0;

    if (size < 12)
        return 0;
    length = d[8] | d[9] << 8 | d[10] << 16 | (size_t)d[11] << 24;
    if (length > 64 || length > size - 12)
        return 0;
    for (i = 12; i + 6 < 12 + length; i++)
        if (memcmp(d + i, "level_", 6) == 0) {
            for (digits = i + 6; digits < 12 + length && d[digits] >= '0' && d[digits] <= '9';
                 digits++)
                if ((level = level * 10 + d[digits] - '0') > 999)
                    return 0;
            return digits == 12 + length ? level : 0;
        }
    return 0;
}
