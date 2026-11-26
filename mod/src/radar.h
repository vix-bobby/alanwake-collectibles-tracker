/* Markers on the game's radar for the nearest missing collectible (radar.c). */
#ifndef RADAR_H
#define RADAR_H

#include "profile.h"

struct radar_host {
    void (*log)(const char *format, ...);
    /* Copy the current profile and whether markers are wanted (the radar_markers option);
     * returns 0 if no profile could be read. */
    int (*state)(struct profile *profile, int *markers);
};

/* Install the radar hooks if the running program is the supported build of the game.
 * Returns 0 if nothing was installed: then nothing in the game was changed. */
int radar_install(const struct radar_host *host);

/* A second message line for switching markers on: where they show, or why they cannot. */
const char *radar_note(void);

#endif
