/* Native test: print the menu suffix of every episode and chapter for a profile. */
#include <stdio.h>
#include <wchar.h>

#include "../src/counts.h"

int main(int argc, char **argv)
{
    static unsigned char data[65536];
    static const char *kinds[] = {"NAME_EPISODE_%d", "NAME_LEVEL_%d", "NAME_EPISODE_SPECIAL_%d"};
    struct profile p;
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : NULL;
    int k, n;

    if (!f || profile_parse(data, fread(data, 1, sizeof data, f), &p) != 0)
        return 1;
    fclose(f);
    for (k = 0; k < 3; k++)
        for (n = 1; n <= 20; n++) {
            char id[32];
            wchar_t suffix[128];

            snprintf(id, sizeof id, kinds[k], n);
            if (menu_suffix(&p, id, argc > 2, suffix, 128))
                printf("%s%ls\n", id, suffix);
        }
    return 0;
}
