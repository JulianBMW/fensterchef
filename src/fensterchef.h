#ifndef FENSTERCHEF_H
#define FENSTERCHEF_H

#include <xcb/xcb.h>

#include <stdio.h>

#ifdef DEBUG
#define LOG(fp, fmt, ...) fprintf((fp), (fmt), ##__VA_ARGS__)
#else
#define LOG(fp, fmt, ...)
#endif

/* xcb server connection */
extern xcb_connection_t     *g_dpy;
/* screens */
extern xcb_screen_t         **g_screens;
extern uint32_t             g_screen_count;
/* the screen being used */
extern uint32_t             g_screen_no;
/* general purpose values */
extern uint32_t             g_values[5];
/* 1 while the window manager is running */
extern int                  g_running;

/* init the connection to xcb */
void init_connection(void);
/* log the screens information to a file */
void log_screens(FILE *fp);
/* initialize the screens */
void init_screens(void);
/* grab the keybinds so we receive the keypress events for them */
void grab_keys(void);
/* subscribe to event substructe redirecting so that we receive map/unmap
 * requests */
int take_control(void);
/* focuses a particular window so that it receives keyboard input */
void set_focus_window(xcb_drawable_t win);
/* handle the mapping of a new window */
void accept_new_window(xcb_drawable_t win);
/* handle the next event xcb has */
void handle_event(void);
/* close the connection to the xcb server */
void close_connection(void);

#endif
