#include <unistd.h>
#include <string.h> // strcmp

#include "action.h"
#include "configuration.h"
#include "fensterchef.h"
#include "frame.h"
#include "log.h"
#include "tiling.h"
#include "window_list.h"

/* all actions and their string representation */
static const char *action_strings[ACTION_MAX] = {
    [ACTION_NULL] = NULL,

    [ACTION_NONE] = "NONE",
    [ACTION_RELOAD_CONFIGURATION] = "START-TERMINAL",
    [ACTION_START_TERMINAL] = "START-TERMINAL",
    [ACTION_NEXT_WINDOW] = "NEXT-WINDOW",
    [ACTION_PREV_WINDOW] = "PREV-WINDOW",
    [ACTION_REMOVE_FRAME] = "REMOVE-FRAME",
    [ACTION_CHANGE_WINDOW_STATE] = "CHANGE-WINDOW-STATE",
    [ACTION_TRAVERSE_FOCUS] = "TRAVERSE-FOCUS",
    [ACTION_TOGGLE_FULLSCREEN] = "TOGGLE-FULLSCREEN",
    [ACTION_SPLIT_HORIZONTALLY] = "SPLIT-HORIZONTALLY",
    [ACTION_SPLIT_VERTICALLY] = "SPLIT-VERTICALLY",
    [ACTION_MOVE_UP] = "MOVE-UP",
    [ACTION_MOVE_LEFT] = "MOVE-LEFT",
    [ACTION_MOVE_RIGHT] = "MOVE-RIGHT",
    [ACTION_MOVE_DOWN] = "MOVE-DOWN",
    [ACTION_SHOW_WINDOW_LIST] = "SHOW-WINDOW-LIST",
    [ACTION_QUIT_WM] = "QUIT-WM",
};

/* Get an action from a string. */
action_t convert_string_to_action(const char *string)
{
    for (action_t i = ACTION_FIRST_ACTION; i < ACTION_MAX; i++) {
        if (strcasecmp(action_strings[i], string) == 0) {
            return i;
        }
    }
    return ACTION_NULL;
}

/* Get a string version of an action. */
const char *convert_action_to_string(action_t action)
{
    return action_strings[action];
}

/* Start a terminal.
 */
static void start_terminal(void)
{
    if (fork() == 0) {
        execl("/usr/bin/xterm", "/usr/bin/xterm", NULL);
    }
}

/* Show the given window and focus it.
 *
 * @window can be NULL to emit a message to the user.
 */
static void set_active_window(Window *window)
{
    if (window == NULL) {
        set_notification(UTF8_TEXT("No other window"),
                focus_frame->x + focus_frame->width / 2,
                focus_frame->y + focus_frame->height / 2);
        return;
    }

    show_window(window);
    set_window_above(window);
}

/* Shows the user the window list and lets the user select a window to focus. */
static void show_window_list(void)
{
    Window *window;

    window = select_window_from_list();
    if (window == NULL) {
        return;
    }

    if (window->state.is_visible) {
        set_window_above(window);
    } else {
        show_window(window);
    }

    set_focus_window_with_frame(window);
}

/* Do the given action, the action codes are `ACTION_*`. */
void do_action(action_t action)
{
    Frame *frame;

    switch (action) {
    /* invalid action value */
    case ACTION_NULL:
        ERR("tried to do NULL action\n");
        break;

    /* do nothing */
    case ACTION_NONE:
        break;

    /* reload the configuration file */
    case ACTION_RELOAD_CONFIGURATION:
        reload_user_configuration();
        break;

    /* open a terminal window */
    case ACTION_START_TERMINAL:
        start_terminal();
        break;

    /* go to the next window in the window list */
    case ACTION_NEXT_WINDOW:
        set_active_window(get_next_hidden_window(focus_frame->window));
        break;

    /* go to the previous window in the window list */
    case ACTION_PREV_WINDOW:
        set_active_window(get_previous_hidden_window(focus_frame->window));
        break;

    /* remove the current frame */
    /* TODO: make it so this hides a popup window as well? */
    case ACTION_REMOVE_FRAME:
        if (remove_frame(focus_frame) != 0) {
            set_notification(UTF8_TEXT("Can not remove the last frame"),
                    focus_frame->x + focus_frame->width / 2,
                    focus_frame->y + focus_frame->height / 2);
        }
        break;

    /* changes a popup window to a tiling window and vise versa */
    case ACTION_CHANGE_WINDOW_STATE:
        if (focus_window == NULL) {
            break;
        }
        set_window_mode(focus_window, focus_window->state.mode == WINDOW_MODE_TILING ?
                WINDOW_MODE_POPUP : WINDOW_MODE_TILING, true);
        break;

    /* changes the window that was in focus before the current one */
    case ACTION_TRAVERSE_FOCUS:
        traverse_focus_chain(-1);
        if (focus_window != NULL) {
            set_window_above(focus_window);
        }
        break;

    /* toggles the fullscreen state of the currently focused window */
    case ACTION_TOGGLE_FULLSCREEN:
        if (focus_window != NULL) {
            set_window_mode(focus_window,
                    focus_window->state.mode == WINDOW_MODE_FULLSCREEN ?
                    focus_window->state.previous_mode : WINDOW_MODE_FULLSCREEN, true);
        }
        break;

    /* split the current frame horizontally */
    case ACTION_SPLIT_HORIZONTALLY:
        split_frame(focus_frame, false);
        break;

    /* split the current frame vertically */
    case ACTION_SPLIT_VERTICALLY:
        split_frame(focus_frame, true);
        break;

    /* move to the frame above the current one */
    case ACTION_MOVE_UP:
        frame = get_frame_at_position(focus_frame->x, focus_frame->y - 1);
        if (frame != NULL) {
            set_focus_frame(frame);
        }
        break;

    /* move to the frame left of the current one */
    case ACTION_MOVE_LEFT:
        frame = get_frame_at_position(focus_frame->x - 1, focus_frame->y);
        if (frame != NULL) {
            set_focus_frame(frame);
        }
        break;

    /* move to the frame right of the current one */
    case ACTION_MOVE_RIGHT:
        frame = get_frame_at_position(focus_frame->x + focus_frame->width, focus_frame->y);
        if (frame != NULL) {
            set_focus_frame(frame);
        }
        break;

    /* move to the frame below the current one */
    case ACTION_MOVE_DOWN:
        frame = get_frame_at_position(focus_frame->x, focus_frame->y + focus_frame->height);
        if (frame != NULL) {
            set_focus_frame(frame);
        }
        break;

    /* show the interactive window list */
    case ACTION_SHOW_WINDOW_LIST:
        show_window_list();
        break;

    /* quits the window manager */
    case ACTION_QUIT_WM:
        quit_fensterchef(EXIT_SUCCESS);
        break;

    /* not a real action */
    case ACTION_MAX:
        break;
    }
}
