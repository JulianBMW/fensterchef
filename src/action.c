#include <unistd.h>
#include <string.h> // strcmp

#include "action.h"
#include "configuration.h"
#include "event.h"
#include "fensterchef.h"
#include "frame.h"
#include "log.h"
#include "tiling.h"
#include "utility.h"
#include "window_list.h"

/* all actions and their string representation */
static const struct {
    /* name of the action */
    const char *name;
    /* data type of the action parameter */
    parser_data_type_t data_type;
} action_information[ACTION_MAX] = {
    [ACTION_NULL] = { NULL, 0 },

    [ACTION_NONE] = { "NONE", PARSER_DATA_TYPE_VOID },
    [ACTION_RELOAD_CONFIGURATION] = { "RELOAD-CONFIGURATION", PARSER_DATA_TYPE_VOID },
    [ACTION_CLOSE_WINDOW] = { "CLOSE-WINDOW", PARSER_DATA_TYPE_VOID },
    [ACTION_MINIMIZE_WINDOW] = { "MINIMIZE-WINDOW", PARSER_DATA_TYPE_VOID },
    [ACTION_FOCUS_WINDOW] = { "FOCUS-WINDOW", PARSER_DATA_TYPE_VOID },
    [ACTION_INITIATE_MOVE] = { "INITIATE-MOVE", PARSER_DATA_TYPE_VOID },
    [ACTION_INITIATE_RESIZE] = { "INITIATE-RESIZE", PARSER_DATA_TYPE_VOID },
    [ACTION_NEXT_WINDOW] = { "NEXT-WINDOW", PARSER_DATA_TYPE_VOID },
    [ACTION_PREVIOUS_WINDOW] = { "PREVIOUS-WINDOW", PARSER_DATA_TYPE_VOID },
    [ACTION_REMOVE_FRAME] = { "REMOVE-FRAME", PARSER_DATA_TYPE_VOID },
    [ACTION_TOGGLE_TILING] = { "TOGGLE-TILING", PARSER_DATA_TYPE_VOID },
    [ACTION_TRAVERSE_FOCUS] = { "TRAVERSE-FOCUS", PARSER_DATA_TYPE_VOID },
    [ACTION_TOGGLE_FULLSCREEN] = { "TOGGLE-FULLSCREEN", PARSER_DATA_TYPE_VOID },
    [ACTION_SPLIT_HORIZONTALLY] = { "SPLIT-HORIZONTALLY", PARSER_DATA_TYPE_VOID },
    [ACTION_SPLIT_VERTICALLY] = { "SPLIT-VERTICALLY", PARSER_DATA_TYPE_VOID },
    [ACTION_MOVE_UP] = { "MOVE-UP", PARSER_DATA_TYPE_VOID },
    [ACTION_MOVE_LEFT] = { "MOVE-LEFT", PARSER_DATA_TYPE_VOID },
    [ACTION_MOVE_RIGHT] = { "MOVE-RIGHT", PARSER_DATA_TYPE_VOID },
    [ACTION_MOVE_DOWN] = { "MOVE-DOWN", PARSER_DATA_TYPE_VOID },
    [ACTION_SHOW_WINDOW_LIST] = { "SHOW-WINDOW-LIST", PARSER_DATA_TYPE_VOID },
    [ACTION_RUN] = { "RUN", PARSER_DATA_TYPE_STRING },
    [ACTION_SHOW_MESSAGE] = { "SHOW-MESSAGE", PARSER_DATA_TYPE_STRING },
    [ACTION_SHOW_MESSAGE_RUN] = { "SHOW-MESSAGE-RUN", PARSER_DATA_TYPE_STRING },
    [ACTION_RESIZE_BY] = { "RESIZE-BY", PARSER_DATA_TYPE_QUAD },
    [ACTION_QUIT] = { "QUIT", PARSER_DATA_TYPE_VOID },
};

/* Get the data type the action expects as parameter. */
parser_data_type_t get_action_data_type(action_t action)
{
    return action_information[action].data_type;
}

/* Get an action from a string. */
action_t convert_string_to_action(const char *string)
{
    for (action_t i = ACTION_FIRST_ACTION; i < ACTION_MAX; i++) {
        if (strcasecmp(action_information[i].name, string) == 0) {
            return i;
        }
    }
    return ACTION_NULL;
}

/* Get a string version of an action. */
const char *convert_action_to_string(action_t action)
{
    return action_information[action].name;
}

/* Create a deep copy of given action array. */
Action *duplicate_actions(Action *actions, uint32_t number_of_actions)
{
    Action *duplicate;

    duplicate = xmemdup(actions, sizeof(*actions) * number_of_actions);
    for (uint32_t i = 0; i < number_of_actions; i++) {
        duplicate_data_value(get_action_data_type(duplicate[i].code),
                &duplicate[i].parameter);
    }
    return duplicate;
}

/* Frees all given actions and the action array itself. */
void free_actions(Action *actions, uint32_t number_of_actions)
{
    for (uint32_t i = 0; i < number_of_actions; i++) {
        clear_data_value(get_action_data_type(actions[i].code),
                &actions[i].parameter);
    }
    free(actions);
}

/* Run given shell program. */
static void run_shell(const char *shell)
{
    if (fork() == 0) {
        execl("/bin/sh", "sh", "-c", shell, (char*) NULL);
    }
}

/* Run a shell and get the output. */
static char *run_shell_and_get_output(const char *shell)
{
    FILE *process;
    char *line;
    size_t length, capacity;

    process = popen(shell, "r");
    if (process == NULL) {
        return NULL;
    }
    capacity = 128;
    line = xmalloc(capacity);
    length = 0;
    for (int c; (c = fgetc(process)) != EOF && c != '\n'; ) {
        if (length + 1 == capacity) {
            capacity *= 2;
            RESIZE(line, capacity);
        }
        line[length++] = c;
    }
    line[length] = '\0';
    pclose(process);
    return line;
}

/* Show the given window and focus it.
 *
 * @window can be NULL to emit a message to the user.
 */
static void set_active_window(Window *window)
{
    if (window == NULL) {
        set_notification((utf8_t*) "No other window",
                focus_frame->x + focus_frame->width / 2,
                focus_frame->y + focus_frame->height / 2);
        return;
    }

    show_window(window);
    set_window_above(window);
    set_focus_window_with_frame(window);
}

/* Show the user the window list and let the user select a window to focus. */
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

/* Resize the current window or current frame if it does not exist. */
static void resize_frame_or_window_by(Window *window, int32_t left, int32_t top,
        int32_t right, int32_t bottom)
{
    Frame *frame;

    if (window == NULL) {
        frame = focus_frame;
        if (frame == NULL) {
            return;
        }
    } else {
        frame = get_frame_of_window(window);
    }

    if (frame != NULL) {
        bump_frame_edge(frame, FRAME_EDGE_LEFT, left);
        bump_frame_edge(frame, FRAME_EDGE_TOP, top);
        bump_frame_edge(frame, FRAME_EDGE_RIGHT, right);
        bump_frame_edge(frame, FRAME_EDGE_BOTTOM, bottom);
    } else {
        right += left;
        bottom += top;
        /* check for underflows */
        if ((int32_t) window->size.width < -right) {
            right = -window->size.width;
        }
        if ((int32_t) window->size.height < -bottom) {
            bottom = -window->size.height;
        }
        set_window_size(window,
                window->position.x - left,
                window->position.y - top,
                window->size.width + right,
                window->size.height + bottom);
    }
}

/* Get a tiling window that is not currently shown and mappable. */
Window *get_next_showable_tiling_window(Window *window)
{
    Window *next;

    if (window == NULL) {
        return last_taken_window;
    }

    /* go forward in the linked list and wrap around */
    next = window;
    while (next = next->next == NULL ? first_window : next->next,
            next != window) {
        if (next->state.was_ever_mapped && !next->state.is_visible &&
                next->state.mode == WINDOW_MODE_TILING) {
            return next;
        }
    }
    return NULL;
}

/* Get a tiling window that is not currently shown and mappable. */
Window *get_previous_showable_tiling_window(Window *window)
{
    Window *valid;
    Window *next;

    if (window == NULL) {
        return last_taken_window;
    }

    /* go forward in the linked list and wrap around, what makes this different
     * is that we store the last window that matched our criteria but don't
     * immediately return
     */
    valid = NULL;
    next = window;
    while (next = next->next == NULL ? first_window : next->next,
            next != window) {
        if (next->state.was_ever_mapped && !next->state.is_visible &&
                next->state.mode == WINDOW_MODE_TILING) {
            valid = next;
        }
    }
    return valid;
}

/* Focus the window above the current window or wrap around at the top. */
void traverse_focus(void)
{
    Window *window, *valid;

    if (focus_window == NULL) {
        return;
    }
    /* try to get a visible window above this window */
    window = focus_window->above;
    while (window != NULL && !window->state.is_visible) {
        window = window->above;
    }

    /* if none found, wrap around and get the bottom window */
    if (window == NULL) {
        window = focus_window->below;
        valid = NULL;
        while (window != NULL) {
            if (window->state.is_visible) {
                valid = window;
            }
            window = window->below;
        }
        window = valid;
    }
    set_active_window(window);
}

/* Do the given action. */
void do_action(const Action *action, Window *window)
{
    Frame *frame;
    char *shell;

    switch (action->code) {
    /* invalid action value */
    case ACTION_NULL:
        LOG_ERROR(NULL, "tried to do NULL action");
        break;

    /* do nothing */
    case ACTION_NONE:
        break;

    /* reload the configuration file */
    case ACTION_RELOAD_CONFIGURATION:
        reload_requested = true;
        break;

    /* closes the currently active window */
    case ACTION_CLOSE_WINDOW:
        if (window == NULL) {
            break;
        }
        close_window(window);
        break;

    /* hide the currently active window */
    case ACTION_MINIMIZE_WINDOW:
        if (window == NULL) {
            break;
        }
        hide_window(window);
        break;

    /* go to the next window in the window list */
    case ACTION_NEXT_WINDOW:
        set_active_window(get_next_showable_tiling_window(focus_frame->window));
        break;

    /* focus a window */
    case ACTION_FOCUS_WINDOW:
        set_focus_window(window);
        break;

    /* start moving a window with the mouse */
    case ACTION_INITIATE_MOVE:
        if (window == NULL) {
            break;
        }
        initiate_window_move_resize(window, 0, 0, 0);
        break;

    /* start resizing a window with the mouse */
    case ACTION_INITIATE_RESIZE:
        if (window == NULL) {
            break;
        }
        initiate_window_move_resize(window, 0, 0, 0);
        break;

    /* go to the previous window in the window list */
    case ACTION_PREVIOUS_WINDOW:
        set_active_window(get_previous_showable_tiling_window(focus_frame->window));
        break;

    /* remove the current frame */
    case ACTION_REMOVE_FRAME:
        if (remove_frame(focus_frame) != 0) {
            set_notification((utf8_t*) "Can not remove the last frame",
                    focus_frame->x + focus_frame->width / 2,
                    focus_frame->y + focus_frame->height / 2);
        }
        break;

    /* changes a popup window to a tiling window and vise versa */
    case ACTION_TOGGLE_TILING:
        if (window == NULL) {
            break;
        }
        set_window_mode(window,
                window->state.mode == WINDOW_MODE_TILING ?
                WINDOW_MODE_POPUP : WINDOW_MODE_TILING, true);
        break;

    /* focus the window above the current window or wrap around at the top */
    case ACTION_TRAVERSE_FOCUS:
        traverse_focus();
        break;

    /* toggles the fullscreen state of the currently focused window */
    case ACTION_TOGGLE_FULLSCREEN:
        if (window != NULL) {
            set_window_mode(window,
                    window->state.mode == WINDOW_MODE_FULLSCREEN ?
                    window->state.previous_mode : WINDOW_MODE_FULLSCREEN,
                    true);
        }
        break;

    /* split the current frame horizontally */
    case ACTION_SPLIT_HORIZONTALLY:
        split_frame(focus_frame, FRAME_SPLIT_HORIZONTALLY);
        break;

    /* split the current frame vertically */
    case ACTION_SPLIT_VERTICALLY:
        split_frame(focus_frame, FRAME_SPLIT_VERTICALLY);
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
    /* TODO: for all below, do not use get_frame_at_position() */
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

    /* quit fensterchef */
    case ACTION_QUIT:
        is_fensterchef_running = false;
        break;

    /* run a shell program */
    case ACTION_RUN:
        run_shell((char*) action->parameter.string);
        break;

    /* show the user a message */
    case ACTION_SHOW_MESSAGE:
        set_notification((utf8_t*) action->parameter.string,
                focus_frame->x + focus_frame->width / 2,
                focus_frame->y + focus_frame->height / 2);
        break;

    /* show a message by getting output from a shell script */
    case ACTION_SHOW_MESSAGE_RUN:
        shell = run_shell_and_get_output((char*) action->parameter.string);
        set_notification((utf8_t*) shell,
                focus_frame->x + focus_frame->width / 2,
                focus_frame->y + focus_frame->height / 2);
        free(shell);
        break;

    /* resize the edges of the current window */
    case ACTION_RESIZE_BY:
        resize_frame_or_window_by(window, action->parameter.quad[0],
                action->parameter.quad[1],
                action->parameter.quad[2],
                action->parameter.quad[3]);
        break;

    /* not a real action */
    case ACTION_MAX:
        break;
    }
}
