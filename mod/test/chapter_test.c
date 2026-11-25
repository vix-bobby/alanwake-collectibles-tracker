/* Native test: chapter_test CONFIG LEVEL prints the overlay text;
 * chapter_test --level FILE prints the level of a save file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/checkpoint.h"
#include "../src/counts.h"

int main(int argc, char **argv)
{
    static unsigned char data[65536];
    static char text[900];
    struct profile p;
    int save = argc == 3 && strcmp(argv[1], "--level") == 0;
    FILE *f = argc == 3 ? fopen(argv[save ? 2 : 1], "rb") : NULL;
    size_t size;

    if (!f)
        return 2;
    size = fread(data, 1, save ? 128 : sizeof data, f);
    fclose(f);
    if (save) {
        printf("%d\n", checkpoint_level(data, size));
        return 0;
    }
    if (profile_parse(data, size, &p) != 0 || !chapter_text(&p, atoi(argv[2]), text, sizeof text))
        return 1;
    fputs(text, stdout);
    return 0;
}
