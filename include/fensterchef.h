#ifndef FENSTERCHEF_H
#define FENSTERCHEF_H

#include <fontconfig/fontconfig.h>

#include <xcb/xcb.h>
#include <xcb/xcb_ewmh.h>
#include <xcb/render.h>

#include <stdbool.h>
#include <stdio.h>

#include "utf8.h"

#define FENSTERCHEF_NAME "fensterchef"

#define FENSTERCHEF_CONFIGURATION ".config/fensterchef/fensterchef.config"

/* true while the window manager is running */
extern bool is_fensterchef_running;

/* Close the connection to xcb and exit the program with given exit code. */
void quit_fensterchef(int exit_code);

/* Show the notification window with given message at given coordinates for
 * NOTIFICATION_DURATION seconds.
 *
 * @message UTF-8 string.
 * @x Center x position.
 * @y Center y position.
 */
void set_notification(const uint8_t *message, int32_t x, int32_t y);

#endif
