/* Native test: print counts and found numbers parsed from a profile file. */
#include <stdio.h>

#include "../src/profile.h"

int main(int argc, char **argv)
{
    static unsigned char data[65536];
    struct profile p;
    FILE *f = argc > 1 ? fopen(argv[1], "rb") : NULL;
    size_t size;
    int c, n;

    if (!f)
        return 2;
    size = fread(data, 1, sizeof data, f);
    fclose(f);
    if (profile_parse(data, size, &p) != 0)
        return 1;
    for (c = 0; c < CAT_COUNT; c++) {
        printf("%s %d:", c ? CATEGORIES[c].key : "MANUSCRIPTPAGES", p.count[c]);
        for (n = 0; n < 256; n++)
            if (p.found[c][n])
                printf(" %d", n);
        printf("\n");
    }
    return 0;
}
