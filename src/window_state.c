#include <inttypes.h>

#include "configuration.h"
#include "fensterchef.h"
#include "frame.h"
#include "log.h"
#include "monitor.h"
#include "root_properties.h"
#include "tiling.h"
#include "utility.h"
#include "window.h"

/* The whole purpose of this file is to detect the initial state of a window
 * and to handle a case where a window changes its window state.
 */

/* Predicts what kind of mode the window should be in.
 * TODO: should this be adjustable in the user configuration?
 */
window_mode_t predict_window_mode(Window *window)
{
    /* direct checks */
    if (has_state(&window->properties, ATOM(_NET_WM_STATE_FULLSCREEN))) {
        return WINDOW_MODE_FULLSCREEN;
    }
    if (has_window_type(&window->properties, ATOM(_NET_WM_WINDOW_TYPE_DOCK))) {
        return WINDOW_MODE_DOCK;
    }

    /* if this window has strut, it must be a dock window */
    if (!is_strut_empty(&window->properties.strut)) {
        return WINDOW_MODE_DOCK;
    }

    /* transient windows are popup windows */
    if (window->properties.transient_for != 0) {
        return WINDOW_MODE_POPUP;
    }

    /* tiling windows have the normal window type */
    if (has_window_type(&window->properties,
                ATOM(_NET_WM_WINDOW_TYPE_NORMAL))) {
        return WINDOW_MODE_TILING;
    }

    /* popup windows have an equal minimum and maximum size */
    if ((window->properties.size_hints.flags &
                (XCB_ICCCM_SIZE_HINT_P_MIN_SIZE |
                 XCB_ICCCM_SIZE_HINT_P_MAX_SIZE)) ==
                (XCB_ICCCM_SIZE_HINT_P_MIN_SIZE |
                 XCB_ICCCM_SIZE_HINT_P_MAX_SIZE) &&
            (window->properties.size_hints.min_width ==
                window->properties.size_hints.max_width ||
            window->properties.size_hints.min_height ==
                window->properties.size_hints.max_height)) {
        return WINDOW_MODE_POPUP;
    }

    /* popup windows have a special window type */
    if (window->properties.types != NULL) {
        return WINDOW_MODE_POPUP;
    }

    /* fall back to tiling window */
    return WINDOW_MODE_TILING;
}

/* Check if @window has a visible border currently. */
bool has_window_border(Window *window)
{
    if (!window->state.is_visible) {
        return false;
    }
    /* tiling windows always have a border */
    if (window->state.mode == WINDOW_MODE_TILING) {
        return true;
    }
    /* fullscreen and dock windows have no border */
    if (window->state.mode != WINDOW_MODE_POPUP) {
        return false;
    }
    /* if the window has borders itself (not set by the window manager) */
    if ((window->properties.motif_wm_hints.flags &
                MOTIF_WM_HINTS_DECORATIONS)) {
        return false;
    }
    return true;
}

/* Set the window size and position according to the size hints. */
static void configure_popup_size(Window *window)
{
    Monitor *monitor;
    int32_t x, y;
    uint32_t width, height;

    monitor = get_monitor_from_rectangle(window->position.x, window->position.y,
            window->size.width, window->size.height);

    /* if the window never had a popup size, use the size hints to get a size
     * that the window prefers
     */
    if (window->popup_size.width == 0) {
        if ((window->properties.size_hints.flags & XCB_ICCCM_SIZE_HINT_P_SIZE)) {
            width = window->properties.size_hints.width;
            height = window->properties.size_hints.height;
        } else {
            width = monitor->size.width * 2 / 3;
            height = monitor->size.height * 2 / 3;
        }

        if ((window->properties.size_hints.flags & XCB_ICCCM_SIZE_HINT_P_MIN_SIZE)) {
            width = MAX(width, (uint32_t) window->properties.size_hints.min_width);
            height = MAX(height, (uint32_t) window->properties.size_hints.min_height);
        }

        if ((window->properties.size_hints.flags & XCB_ICCCM_SIZE_HINT_P_MAX_SIZE)) {
            width = MIN(width, (uint32_t) window->properties.size_hints.max_width);
            height = MIN(height, (uint32_t) window->properties.size_hints.max_height);
        }

        if ((window->properties.size_hints.flags & XCB_ICCCM_SIZE_HINT_P_POSITION)) {
            x = window->properties.size_hints.x;
            y = window->properties.size_hints.y;
        } else {
            x = monitor->position.x + (monitor->size.width - width) / 2;
            y = monitor->position.y + (monitor->size.height - height) / 2;
        }

        window->popup_position.x = x;
        window->popup_position.y = y;
        window->popup_size.width = width;
        window->popup_size.height = height;
    } else {
        x = window->popup_position.x;
        y = window->popup_position.y;
        width = window->popup_size.width;
        height = window->popup_size.height;
    }

    /* consider the window gravity, i.e. where the window wants to be */
    if ((window->properties.size_hints.flags &
                XCB_ICCCM_SIZE_HINT_P_WIN_GRAVITY)) {
        adjust_for_window_gravity(monitor, &x, &y, width, height,
                window->properties.size_hints.win_gravity);
    }

    set_window_size(window, x, y, width, height);
}

/* Sets the position and size of the window to fullscreen. */
static void configure_fullscreen_size(Window *window)
{
    Monitor *monitor;

    if (window->properties.fullscreen_monitors.top !=
            window->properties.fullscreen_monitors.bottom) {
        set_window_size(window, window->properties.fullscreen_monitors.left,
                window->properties.fullscreen_monitors.top,
                window->properties.fullscreen_monitors.right -
                    window->properties.fullscreen_monitors.left,
                window->properties.fullscreen_monitors.bottom -
                    window->properties.fullscreen_monitors.left);
    } else {
        monitor = get_monitor_from_rectangle(window->position.x,
                window->position.y, window->size.width, window->size.height);
        set_window_size(window, monitor->position.x, monitor->position.y,
                monitor->size.width, monitor->size.height);
    }
}

/* Sets the position and size of the window to a dock window. */
static void configure_dock_size(Window *window)
{
    Monitor *monitor;
    int32_t x, y;
    uint32_t width, height;

    if ((window->properties.size_hints.flags & XCB_ICCCM_SIZE_HINT_P_SIZE)) {
        width = window->properties.size_hints.width;
        height = window->properties.size_hints.height;
    } else {
        width = 0;
        height = 0;
    }

    if ((window->properties.size_hints.flags & XCB_ICCCM_SIZE_HINT_P_POSITION)) {
        x = window->properties.size_hints.x;
        y = window->properties.size_hints.y;
    } else {
        x = window->position.x;
        y = window->position.y;
    }

    monitor = get_monitor_from_rectangle(x, y, 1, 1);

    /* if the window does not specify a size itself, then do it based on the
     * strut the window defines, reasoning is that when the window wants to
     * occupy screen space, then it should be within that occupied space
     */
    if (width == 0 || height == 0) {
        if (window->properties.strut.reserved.left != 0) {
            x = monitor->position.x;
            y = window->properties.strut.left_start_y;
            width = window->properties.strut.reserved.left;
            height = window->properties.strut.left_end_y -
                window->properties.strut.left_start_y + 1;
        } else if (window->properties.strut.reserved.top != 0) {
            x = window->properties.strut.top_start_x;
            y = monitor->position.y;
            width = window->properties.strut.top_end_x -
                window->properties.strut.top_start_x + 1;
            height = window->properties.strut.reserved.top;
        } else if (window->properties.strut.reserved.right != 0) {
            x = monitor->position.x + monitor->size.width -
                window->properties.strut.reserved.right;
            y = window->properties.strut.right_start_y;
            width = window->properties.strut.reserved.right;
            height = window->properties.strut.right_end_y -
                window->properties.strut.right_start_y + 1;
        } else if (window->properties.strut.reserved.bottom != 0) {
            x = window->properties.strut.bottom_start_x;
            y = monitor->position.y + monitor->size.height -
                window->properties.strut.reserved.bottom;
            width = window->properties.strut.bottom_end_x -
                window->properties.strut.bottom_start_x + 1;
            height = window->properties.strut.reserved.bottom;
        } else {
            /* TODO: what to do here? */
            width = 64;
            height = 32;
        }
    }

    /* consider the window gravity, i.e. where the window wants to be */
    if ((window->properties.size_hints.flags &
                XCB_ICCCM_SIZE_HINT_P_WIN_GRAVITY)) {
        adjust_for_window_gravity(monitor, &x, &y, width, height,
            window->properties.size_hints.win_gravity);
    }

    set_window_size(window, x, y, width, height);
}

/* Synchronize the _NET_WM_ALLOWED actions X property. */
void synchronize_allowed_actions(Window *window)
{
    const xcb_atom_t atom_lists[WINDOW_MODE_MAX][16] = {
        [WINDOW_MODE_TILING] = {
            ATOM(_NET_WM_ACTION_MAXIMIZE_HORZ),
            ATOM(_NET_WM_ACTION_MAXIMIZE_VERT),
            ATOM(_NET_WM_ACTION_FULLSCREEN),
            ATOM(_NET_WM_ACTION_CHANGE_DESKTOP),
            ATOM(_NET_WM_ACTION_CLOSE),
            XCB_NONE,
        },

        [WINDOW_MODE_POPUP] = {
            ATOM(_NET_WM_ACTION_MOVE),
            ATOM(_NET_WM_ACTION_RESIZE),
            ATOM(_NET_WM_ACTION_MINIMIZE),
            ATOM(_NET_WM_ACTION_SHADE),
            ATOM(_NET_WM_ACTION_STICK),
            ATOM(_NET_WM_ACTION_MAXIMIZE_HORZ),
            ATOM(_NET_WM_ACTION_MAXIMIZE_VERT),
            ATOM(_NET_WM_ACTION_FULLSCREEN),
            ATOM(_NET_WM_ACTION_CHANGE_DESKTOP),
            ATOM(_NET_WM_ACTION_CLOSE),
            ATOM(_NET_WM_ACTION_ABOVE),
            ATOM(_NET_WM_ACTION_BELOW),
            XCB_NONE,
        },

        [WINDOW_MODE_FULLSCREEN] = {
            ATOM(_NET_WM_ACTION_CHANGE_DESKTOP),
            ATOM(_NET_WM_ACTION_CLOSE),
            ATOM(_NET_WM_ACTION_ABOVE),
            ATOM(_NET_WM_ACTION_BELOW),
            XCB_NONE,
        },

        [WINDOW_MODE_DOCK] = {
            XCB_NONE,
        },
    };

    const xcb_atom_t *list;
    uint32_t list_length;

    list = atom_lists[window->state.mode];
    for (list_length = 0; list_length < SIZE(atom_lists[0]); list_length++) {
        if (list[list_length] == XCB_NONE) {
            break;
        }
    }

    xcb_change_property(connection, XCB_PROP_MODE_REPLACE,
            window->properties.window, ATOM(_NET_WM_ALLOWED_ACTIONS),
            XCB_ATOM_ATOM, 32, list_length * sizeof(*list), list);
}

/* Changes the window state to given value and reconfigures the window only
 * if the mode changed.
 */
void set_window_mode(Window *window, window_mode_t mode, bool force_mode)
{
    if (window->state.mode == mode ||
            (window->state.is_mode_forced && !force_mode)) {
        return;
    }

    LOG("transition window mode of %" PRIu32 " from %u to %u (%s)\n",
            window->number, window->state.mode, mode,
            force_mode ? "forced" : "not forced");

    window->state.is_mode_forced = force_mode;

    if (window->state.is_visible) {
        /* pop out from tiling layout */
        if (window->state.mode == WINDOW_MODE_TILING) {
            Frame *const frame = get_frame_of_window(window);
            frame->window = NULL;
            if (configuration.tiling.auto_fill_void) {
                fill_empty_frame(frame);
            }
        }

        switch (mode) {
        case WINDOW_MODE_TILING:
            if (focus_frame->window == focus_window) {
                set_focus_window(window);
            }

            if (focus_frame->window != NULL) {
                hide_window_abruptly(focus_frame->window);
            }

            focus_frame->window = window;
            reload_frame(focus_frame);
            break;

        case WINDOW_MODE_POPUP:
            configure_popup_size(window);
            break;

        case WINDOW_MODE_FULLSCREEN:
            configure_fullscreen_size(window);
            break;

        case WINDOW_MODE_DOCK:
            configure_dock_size(window);
            break;

        /* not a real window mode */
        case WINDOW_MODE_MAX:
            break;
        }

        set_window_above(window);
    } else {
        /* make sure the window is no longer in the taken list */
        if (window->state.mode == WINDOW_MODE_TILING) {
            unlink_window_from_taken_list(window);
        }
    }

    /* set the window border */
    switch (mode) {
    /* tiling windows have the default window border */
    case WINDOW_MODE_TILING:
        general_values[0] = configuration.border.size;
        xcb_configure_window(connection, window->properties.window,
                XCB_CONFIG_WINDOW_BORDER_WIDTH, general_values);
        break;

    /* popup windows usually have a window border but... */
    case WINDOW_MODE_POPUP:
        /* ...windows that set this flag have their own border/frame */
        if ((window->properties.motif_wm_hints.flags &
                    MOTIF_WM_HINTS_DECORATIONS)) {
            general_values[0] = 0;
        } else {
            general_values[0] = configuration.border.size;
        }
        xcb_configure_window(connection, window->properties.window,
                XCB_CONFIG_WINDOW_BORDER_WIDTH, general_values);
        break;

    /* these windows have no border */
    case WINDOW_MODE_FULLSCREEN:
    case WINDOW_MODE_DOCK:
        general_values[0] = 0;
        xcb_configure_window(connection, window->properties.window,
                XCB_CONFIG_WINDOW_BORDER_WIDTH, general_values);
        break;

    /* not a real window mode */
    case WINDOW_MODE_MAX:
        break;
    }

    window->state.previous_mode = window->state.mode;
    window->state.mode = mode;

    synchronize_allowed_actions(window);
}

/* Show the window by mapping it to the X server. */
void show_window(Window *window)
{
    Window *last, *previous;

    if (window->state.is_visible) {
        LOG("tried to show already shown window: %" PRIu32 "\n",
                window->number);
        return;
    }

    /* assign the first id to the window if it is first mapped */
    if (!window->state.was_ever_mapped) {
        for (last = first_window; last->next != NULL; last = last->next) {
            if (last->number + 1 < last->next->number) {
                break;
            }
        }
        window->number = last->number + 1;

        LOG("assigned id %" PRIu32 " to window wrapping %" PRIu32 "\n",
                window->number, window->properties.window);

        /* reinsert the window into the main linked list */
        if (last != window) {
            if (window != first_window) {
                previous = first_window;
                while (previous->next != window) {
                    previous = previous->next;
                }
                previous->next = window->next;
            } else {
                first_window = window->next;
            }
            window->next = last->next;
            last->next = window;
        }

        /* note: when a window is mapped first, it is at the top automatically
         */
        /* find a window that was ever mapped */
        previous = first_window;
        while (previous != NULL) {
            if (previous->state.was_ever_mapped) {
                break;
            }
            previous = previous->next;
        }
        /* link it into the Z linked list */
        if (previous != NULL) {
            while (previous->above != NULL) {
                previous = previous->above;
            }
            previous->above = window;
            window->below = previous;
        }

        window->state.was_ever_mapped = true;

        synchronize_root_property(ROOT_PROPERTY_CLIENT_LIST);
    }

    LOG("showing window with id: %" PRIu32 "\n", window->number);

    window->state.is_visible = true;

    previous = NULL;
    switch (window->state.mode) {
    /* the window has to become part of the tiling layout */
    case WINDOW_MODE_TILING: {
        Frame *const frame = get_frame_of_window(window);
        if (frame != NULL) {
            reload_frame(frame);
            break;
        }
        previous = focus_frame->window;

        focus_frame->window = window;
        reload_frame(focus_frame);
    } break;

    /* the window has to show as popup window */
    case WINDOW_MODE_POPUP:
        configure_popup_size(window);
        break;

    /* the window has to show as fullscreen window */
    case WINDOW_MODE_FULLSCREEN:
        configure_fullscreen_size(window);
        break;

    /* the window has to show as dock window */
    case WINDOW_MODE_DOCK:
        configure_dock_size(window);
        break;

    /* not a real window mode */
    case WINDOW_MODE_MAX:
        break;
    }

    xcb_map_window(connection, window->properties.window);

    /* the window is now shown, no longer need it in here */
    unlink_window_from_taken_list(window);

    /* this is delayed because we always want to map first */
    if (previous != NULL) {
        hide_window_abruptly(previous);
    }

    /* check if strut have appeared */
    if (!is_strut_empty(&window->properties.strut)) {
        reconfigure_monitor_frame_sizes();
        synchronize_root_property(ROOT_PROPERTY_WORK_AREA);
    }
}

/* Hide the window by unmapping it from the X server. */
void hide_window(Window *window)
{
    Frame *frame;

    LOG("hiding window with id: %" PRIu32 "\n", window->number);

    if (!window->state.is_visible) {
        LOG("the window is already hidden window\n");
        return;
    }

    window->state.is_visible = false;

    switch (window->state.mode) {
    /* the window is replaced by another window in the tiling layout */
    case WINDOW_MODE_TILING:
        frame = get_frame_of_window(window);

        frame->window = NULL;

        if (configuration.tiling.auto_remove_void) {
            if (frame->parent != NULL) {
                remove_frame(frame);
            }
        } else if (configuration.tiling.auto_fill_void) {
            fill_empty_frame(frame);
            if (window == focus_window) {
                set_focus_window(frame->window);
            }
        }

        /* make sure no broken focus remains */
        if (window == focus_window) {
            set_focus_window(NULL);
        }

        /* link into the taken window list */
        window->previous_taken = last_taken_window;
        last_taken_window = window;
        break;

    /* need to just focus a different window */
    case WINDOW_MODE_POPUP:
    case WINDOW_MODE_FULLSCREEN:
    case WINDOW_MODE_DOCK:
        if (window == focus_window) {
            set_focus_window_with_frame(window->below == NULL ? window->above :
                    window->below);
        }
        break;

    /* not a real window mode */
    case WINDOW_MODE_MAX:
        break;
    }

    xcb_unmap_window(connection, window->properties.window);

    /* check if strut has disappeared */
    if (!is_strut_empty(&window->properties.strut)) {
        reconfigure_monitor_frame_sizes();
        synchronize_root_property(ROOT_PROPERTY_WORK_AREA);
    }
}

/* Wrapper around `hide_window()` that does not touch the tiling or focus. */
void hide_window_abruptly(Window *window)
{
    window_mode_t previous_mode;

    /* do nothing if the window is already hidden */
    if (!window->state.is_visible) {
        return;
    }

    previous_mode = window->state.mode;
    window->state.mode = WINDOW_MODE_MAX;
    hide_window(window);
    window->state.mode = previous_mode;

    /* link into the taken window list */
    window->previous_taken = last_taken_window;
    last_taken_window = window;
    /* make sure there is no invalid focus window */
    if (window == focus_window) {
        set_focus_window(NULL);
    }
}
