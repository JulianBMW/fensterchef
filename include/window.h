#ifndef WINDOW_H
#define WINDOW_H

#include <stdint.h>

#include "bits/window_typedef.h"
#include "bits/frame_typedef.h"

#include "monitor.h"
#include "utility.h"
#include "window_state.h"

#include "x11_management.h"

/* the maximum size of the window */
#define WINDOW_MAXIMUM_SIZE 1000000

/* the minimum length of the window that needs to stay visible */
#define WINDOW_MINIMUM_VISIBLE_SIZE 8

/* the minimum width or height a window can have */
#define WINDOW_MINIMUM_SIZE 4

/* the number the first window gets assigned */
#define FIRST_WINDOW_NUMBER 1

/* A window is a wrapper around an xcb window, it is always part of a global
 * linked list and has a unique id.
 */
struct window {
    /* the window's X properties */
    XProperties properties;

    /* the window state */
    WindowState state;

    /* current window position and size */
    Position position;
    Size size;

    /* position and size when the window was in popup state */
    Position popup_position;
    Size popup_size;

    /* the id of this window */
    uint32_t number;

    /* the focus chain is a linked list containing only visible windows and
     * their relative time when they were focused; the focus chain is cyclic,
     * meaning that all windows within the focus chain have a next_focus and
     * previous_focus
     */

    /* the previous window in the focus chain */
    Window *previous_focus;
    /* the next window in the focus chain */
    Window *next_focus;

    /* the next window in the linked list */
    Window *next;
};

/* the first window in the linked list, the list is sorted increasingly
 * with respect to the window number
 */
extern Window *first_window;

/* the currently focused window */
extern Window *focus_window;

/* Create a window struct and add it to the window list. */
Window *create_window(xcb_window_t xcb);

/* time in seconds to wait for a second close */
#define REQUEST_CLOSE_MAX_DURATION 3

/* Attempt to close a window. If it is the first time, use a friendly method by
 * sending a close request to the window. Call this function again within
 * `REQUEST_CLOSE_MAX_DURATION` to forcefully kill it.
 */
void close_window(Window *window);

/* Destroy given window and removes it from the window linked list.
 * This does NOT destroy the underlying xcb window.
 */
void destroy_window(Window *window);

/* Adjust given @x and @y such that it follows the @window_gravity. */
void adjust_for_window_gravity(Monitor *monitor, int32_t *x, int32_t *y,
        uint32_t width, uint32_t height, uint32_t window_gravity);

/* Set the position and size of a window. */
void set_window_size(Window *window, int32_t x, int32_t y, uint32_t width,
        uint32_t height);

/* Put the window on top of all other windows. */
void set_window_above(Window *window);

/* Get the window before this window in the linked list.
 * This function WRAPS around so
 *  `get_previous_window(first_window)` returns the last window.
 *
 * @window may be NULL, then NULL is also returned.
 */
Window *get_previous_window(Window *window);

/* Get the internal window that has the associated xcb window.
 *
 * @return NULL when none has this xcb window.
 */
Window *get_window_of_xcb_window(xcb_window_t xcb_window);

/* Get the frame this window is contained in.
 *
 * @return NULL when the window is not in any frame.
 */
Frame *get_frame_of_window(const Window *window);

/* Removes a window from the focus list. */
void unlink_window_from_focus_list(Window *window);

/* Check if the window accepts input focus. */
bool does_window_accept_focus(Window *window);

/* Set the focus_window and change the border color. */
void set_focus_window_primitively(Window *window);

/* Set the window that is in focus. */
void set_focus_window(Window *window);

/* Focuses the window before or after the currently focused window. */
void traverse_focus_chain(int direction);

/* Focuses the window guaranteed but also focuse the frame of the window if it
 * has one.
 */
void set_focus_window_with_frame(Window *window);

/* Get a window that is not shown but in the window list coming after
 * the given window or NULL when there is none.
 *
 * @window may be NULL.
 * @return NULL iff there is no hidden window.
 */
Window *get_next_hidden_window(Window *window);

/* Get a window that is not shown but in the window list coming before
 * the given window.
 *
 * @window may be NULL.
 * @return NULL iff there is no hidden window.
 */
Window *get_previous_hidden_window(Window *window);

/* Puts a window into a frame and matches its size.
 *
 * This also disconnects the frame of the window AND the window of the frame
 * from their respective frame and window which makes this a very safe function.
 */
void link_window_and_frame(Window *window, Frame *frame);

#endif
