/* Test build only: a marker on the game's radar for the nearest missing collectible (radar.c). */
#ifndef RADAR_H
#define RADAR_H

#include "profile.h"

struct radar_host {
    void (*log)(const char *format, ...);
    /* Copy the current profile and whether markers are wanted (the radar_markers option);
     * returns 0 if no profile could be read. */
    int (*state)(struct profile *profile, int *markers);
};

/* Stage 1 installs the hooks and only logs; stage 2 may also add markers. Returns 0 if
 * nothing was installed: then nothing in the game was changed. */
int radar_install(const struct radar_host *host, int stage);

/* Called on each presented frame: logs the frame thread once. */
void radar_frame(void);

/* Why markers may not show at the moment, as a second message line ("" if nothing to say). */
const char *radar_note(void);

#endif
