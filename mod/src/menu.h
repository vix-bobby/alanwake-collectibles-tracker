/* Collectible counts in the game's menus, kept up to date while it runs. */
#ifndef MENU_H
#define MENU_H

#include "profile.h"

struct menu_options {
    int enabled; /* show counts */
    int detail;  /* split them by type */
};

struct menu_host {
    void (*log)(const char *format, ...);
    /* Copy the current profile and options; returns 0 if no profile could be read. */
    int (*state)(struct profile *profile, struct menu_options *options);
};

/* Install the menu hooks if the running program is the supported build of the game. With
 * `small`, the counts of list entries are drawn in a smaller size after the name. Returns 0
 * if nothing was installed. */
int menu_install(const struct menu_host *host, int small);

#endif
