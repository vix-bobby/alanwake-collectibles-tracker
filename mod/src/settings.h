/* alanwake_collectibles.ini: on/off choices and the keys that change them. */
#ifndef SETTINGS_H
#define SETTINGS_H

enum {
    OPT_OVERLAY,
    OPT_MENU_COUNTS,
    OPT_MENU_DETAIL,
    OPT_MENU_SMALL,
    OPT_MENU_HOOKS,
    OPT_COUNT
};

struct option {
    const char *name;  /* ini key and key-binding name */
    const char *label; /* shown on screen */
    int enabled;
    int key;           /* virtual-key code F1..F24, 0 = none */
};

extern struct option OPTIONS[OPT_COUNT];

/* Where the overlay sits: a corner, and its distance from the edges in percent of the
 * screen width and height. */
struct position {
    int right, bottom;
    int margin_x, margin_y;
};

extern struct position POSITION;

/* Read the ini file, creating it with explanatory comments if it does not exist. */
void settings_load(const char *path);
/* Flip an option and save it. Returns 0 if it could not be written. */
int settings_toggle(int option);
/* "F8" for VK_F8, "-" for none. */
const char *key_name(int key, char out[8]);

#endif
