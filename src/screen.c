#include <string.h>

#include <xcb/xcb_renderutil.h>

#include "event.h" // randr_event_base
#include "fensterchef.h"
#include "frame.h"
#include "keybind.h"
#include "log.h"
#include "screen.h"
#include "tiling.h"
#include "util.h"
#include "window.h"
#include "xalloc.h"

/* event mask for the root window */
#define ROOT_EVENT_MASK (XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | \
                         XCB_EVENT_MASK_BUTTON_PRESS | \
                         /* when the user adds a monitor (e.g. video
                          * projector), the root window gets a
                          * ConfigureNotify */ \
                         XCB_EVENT_MASK_STRUCTURE_NOTIFY | \
                         XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | \
                         XCB_EVENT_MASK_PROPERTY_CHANGE | \
                         XCB_EVENT_MASK_FOCUS_CHANGE | \
                         XCB_EVENT_MASK_ENTER_WINDOW)

/* if randr is enabled for usage */
static bool randr_enabled = false;

/* the actively used screen */
Screen *screen;

/* Initializes the stock_objects array with graphical objects. */
static inline int init_stock_objects(void)
{
    xcb_window_t                                root;
    xcb_generic_error_t                         *error;
    xcb_render_color_t                          color;
    xcb_render_pictforminfo_t                   *fmt;
    const xcb_render_query_pict_formats_reply_t *fmt_reply;
    xcb_pixmap_t                                pixmap;
    xcb_rectangle_t                             rect;

    root = screen->xcb_screen->root;

    for (uint32_t i = 0; i < STOCK_COUNT; i++) {
        screen->stock_objects[i] = xcb_generate_id(connection);
    }

    general_values[0] = screen->xcb_screen->black_pixel;
    general_values[1] = screen->xcb_screen->white_pixel;
    error = xcb_request_check(connection,
            xcb_create_gc_checked(connection, screen->stock_objects[STOCK_GC],
                root,
                XCB_GC_FOREGROUND | XCB_GC_BACKGROUND, general_values));
    if (error != NULL) {
        log_error(error, "could not create graphics context for notifications: ");
        return 1;
    }

    general_values[0] = screen->xcb_screen->white_pixel;
    general_values[1] = screen->xcb_screen->black_pixel;
    error = xcb_request_check(connection,
            xcb_create_gc_checked(connection,
                screen->stock_objects[STOCK_INVERTED_GC], root,
                XCB_GC_FOREGROUND | XCB_GC_BACKGROUND, general_values));
    if (error != NULL) {
        log_error(error, "could not create inverted graphics context for notifications: ");
        return 1;
    }

    fmt_reply = xcb_render_util_query_formats(connection);

    fmt = xcb_render_util_find_standard_format(fmt_reply,
            XCB_PICT_STANDARD_ARGB_32);

    /* create white pen */
    pixmap = xcb_generate_id(connection);
    xcb_create_pixmap(connection, 32, pixmap, root, 1, 1);

    general_values[0] = XCB_RENDER_REPEAT_NORMAL;
    xcb_render_create_picture(connection,
            screen->stock_objects[STOCK_WHITE_PEN], pixmap, fmt->id,
            XCB_RENDER_CP_REPEAT, general_values);

    rect.x = 0;
    rect.y = 0;
    rect.width = 1;
    rect.height = 1;

    color.alpha = 0xff00;
    color.red = 0xff00;
    color.green = 0xff00;
    color.blue = 0xff00;
    xcb_render_fill_rectangles(connection, XCB_RENDER_PICT_OP_OVER,
            screen->stock_objects[STOCK_WHITE_PEN], color, 1, &rect);

    xcb_free_pixmap(connection, pixmap);

    /* create black pen */
    pixmap = xcb_generate_id(connection);
    xcb_create_pixmap(connection, 32, pixmap, root, 1, 1);

    general_values[0] = XCB_RENDER_REPEAT_NORMAL;
    xcb_render_create_picture(connection, screen->stock_objects[STOCK_BLACK_PEN],
            pixmap, fmt->id,
            XCB_RENDER_CP_REPEAT, general_values);

    color.red = 0x0000;
    color.green = 0x0000;
    color.blue = 0x0000;
    xcb_render_fill_rectangles(connection, XCB_RENDER_PICT_OP_OVER,
            screen->stock_objects[STOCK_BLACK_PEN], color, 1, &rect);

    xcb_free_pixmap(connection, pixmap);

    return 0;
}

/* Create the notification and window list windows. */
static inline int create_utility_windows(void)
{
    xcb_window_t root;
    xcb_generic_error_t *error;
    xcb_icccm_wm_hints_t no_input_hints;

    root = screen->xcb_screen->root;

    no_input_hints.flags = XCB_ICCCM_WM_HINT_INPUT;
    no_input_hints.input = 0;

    screen->check_window = xcb_generate_id(connection);
    error = xcb_request_check(connection, xcb_create_window_checked(connection,
                XCB_COPY_FROM_PARENT, screen->check_window,
                root, -1, -1, 1, 1, 0,
                XCB_WINDOW_CLASS_COPY_FROM_PARENT, XCB_COPY_FROM_PARENT,
                0, NULL));
    if (error != NULL) {
        log_error(error, "could not create check window: ");
        return 1;
    }
    xcb_ewmh_set_wm_name(&ewmh, screen->check_window,
            strlen(FENSTERCHEF_NAME), FENSTERCHEF_NAME);
    xcb_ewmh_set_supporting_wm_check(&ewmh, screen->check_window,
            screen->check_window);

    screen->notification_window = xcb_generate_id(connection);
    general_values[0] = 0x000000;
    error = xcb_request_check(connection, xcb_create_window_checked(connection,
                XCB_COPY_FROM_PARENT, screen->notification_window,
                root, -1, -1, 1, 1, 0,
                XCB_WINDOW_CLASS_COPY_FROM_PARENT, XCB_COPY_FROM_PARENT,
                XCB_CW_BORDER_PIXEL, general_values));
    if (error != NULL) {
        log_error(error, "could not create notification window: ");
        return 1;
    }
    xcb_icccm_set_wm_hints(connection, screen->notification_window,
            &no_input_hints);

    screen->window_list_window = xcb_generate_id(connection);
    general_values[0] = 0x000000;
    general_values[1] = XCB_EVENT_MASK_KEY_PRESS;
    error = xcb_request_check(connection, xcb_create_window_checked(connection,
                XCB_COPY_FROM_PARENT, screen->window_list_window,
                root, -1, -1, 1, 1, 0,
                XCB_WINDOW_CLASS_COPY_FROM_PARENT, XCB_COPY_FROM_PARENT,
                XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK, general_values));
    if (error != NULL) {
        log_error(error, "could not create window list window: ");
        return 1;
    }
    return 0;
}

/* Initializes the WM data of the screen. */
int init_screen(int screen_number)
{
    xcb_window_t root;
    xcb_generic_error_t *error;

    screen = xcalloc(1, sizeof(*screen));
    screen->number = screen_number;
    screen->xcb_screen = ewmh.screens[screen_number];

    root = screen->xcb_screen->root;

    if (init_stock_objects() != 0) {
        return 1;
    }

    general_values[0] = ROOT_EVENT_MASK;
    error = xcb_request_check(connection,
            xcb_change_window_attributes_checked(connection, root,
                XCB_CW_EVENT_MASK, general_values));
    if (error != NULL) {
        log_error(error, "could not change root window mask: ");
        return 1;
    }

    if (create_utility_windows() != 0) {
        return 1;
    }

    /* grabs ALT+Button for moving popup windows */
    xcb_grab_button(connection, 0, root, XCB_EVENT_MASK_BUTTON_PRESS |
            XCB_EVENT_MASK_BUTTON_RELEASE, XCB_GRAB_MODE_ASYNC,
            XCB_GRAB_MODE_ASYNC, root, XCB_NONE, 1, XCB_MOD_MASK_1);
    return 0;
}

/* Create a screenless monitor. */
static Monitor *create_monitor(const char *name, uint32_t name_len)
{
    Monitor *monitor;

    monitor = xcalloc(1, sizeof(*monitor));

    monitor->name = xstrndup(name, name_len);
    monitor->frame = xcalloc(1, sizeof(*monitor->frame));

    return monitor;
}

/* Try to initialize randr for monitor management. */
void init_monitors(void)
{
    const xcb_query_extension_reply_t *extension;
    xcb_generic_error_t *error;
    xcb_randr_query_version_cookie_t version_cookie;
    xcb_randr_query_version_reply_t *version;

    extension = xcb_get_extension_data(connection, &xcb_randr_id);
    if (!extension->present) {
        return;
    }

    version_cookie = xcb_randr_query_version(connection, XCB_RANDR_MAJOR_VERSION,
            XCB_RANDR_MINOR_VERSION);
    version = xcb_randr_query_version_reply(connection, version_cookie, &error);
    if (error != NULL) {
        log_error(error, "could not query randr version: ");
        free(error);
    } else {
        free(version);

        randr_enabled = true;
        randr_event_base = extension->first_event;
    }

    if (randr_enabled) {
        xcb_randr_select_input(connection, screen->xcb_screen->root,
                XCB_RANDR_NOTIFY_MASK_SCREEN_CHANGE |
                XCB_RANDR_NOTIFY_MASK_OUTPUT_CHANGE |
                XCB_RANDR_NOTIFY_MASK_CRTC_CHANGE |
                XCB_RANDR_NOTIFY_MASK_OUTPUT_PROPERTY);
    }

    merge_monitors(query_monitors());
}

/* Get a monitor marked as primary or the first monitor if no monitor is marked
 * as primary.
 */
Monitor *get_primary_monitor(void)
{
    for (Monitor *monitor = screen->monitor; monitor != NULL;
            monitor = monitor->next) {
        if (monitor->primary) {
            return monitor;
        }
    }
    return screen->monitor;
}

/* Get the monitor that overlaps given rectangle the most. */
Monitor *get_monitor_from_rectangle(int32_t x, int32_t y,
        uint32_t width, uint32_t height)
{
    Monitor *best_monitor = NULL;
    uint64_t best_area = 0, area;
    int32_t x_overlap, y_overlap;

    for (Monitor *monitor = screen->monitor; monitor != NULL;
            monitor = monitor->next) {
        x_overlap = MIN(x + width,
                monitor->position.x + monitor->size.width);
        x_overlap -= MAX(x, monitor->position.x);

        y_overlap = MIN(y + height,
                monitor->position.y + monitor->size.height);
        y_overlap -= MAX(y, monitor->position.y);

        if (best_monitor == NULL) {
            best_monitor = monitor;
        }

        if (x_overlap <= 0 || y_overlap <= 0) {
            continue;
        }

        area = x_overlap * y_overlap;
        if (area > best_area) {
            best_monitor = monitor;
            best_area = area;
        }
    }
    return best_monitor;
}

/* Get a monitor with given name from the monitor linked list. */
static Monitor *get_monitor_by_name(Monitor *monitor,
        const char *name, int name_len)
{
    for (; monitor != NULL; monitor = monitor->next) {
        if (strncmp(monitor->name, name, name_len) == 0 &&
                monitor->name[name_len] == '\0') {
            return monitor;
        }
    }
    return NULL;
}

/* Gets a list of monitors that are associated to the screen. */
Monitor *query_monitors(void)
{
    xcb_generic_error_t *error;

    xcb_randr_get_output_primary_cookie_t primary_cookie;
    xcb_randr_get_output_primary_reply_t *primary;
    xcb_randr_output_t primary_output;

    xcb_randr_get_screen_resources_current_cookie_t resources_cookie;
    xcb_randr_get_screen_resources_current_reply_t *resources;

    xcb_randr_output_t *outputs;
    int output_count;

    Monitor *first_monitor, *last_monitor;

    xcb_randr_get_output_info_cookie_t output_cookie;
    xcb_randr_get_output_info_reply_t *output;

    char *name;
    int name_len;

    xcb_randr_get_crtc_info_cookie_t crtc_cookie;
    xcb_randr_get_crtc_info_reply_t *crtc;

    if (!randr_enabled) {
        return NULL;
    }

    primary_cookie = xcb_randr_get_output_primary(connection, screen->xcb_screen->root);
    resources_cookie = xcb_randr_get_screen_resources_current(connection,
            screen->xcb_screen->root);

    primary = xcb_randr_get_output_primary_reply(connection, primary_cookie, NULL);
    if (primary == NULL) {
        primary_output = XCB_NONE;
    } else {
        primary_output = primary->output;
        free(primary);
    }

    resources = xcb_randr_get_screen_resources_current_reply(connection,
            resources_cookie, &error);
    if (error != NULL) {
        log_error(error, "could not get screen resources: ");
        free(error);
        return NULL;
    }

    outputs = xcb_randr_get_screen_resources_current_outputs(resources);
    output_count =
        xcb_randr_get_screen_resources_current_outputs_length(resources);

    first_monitor = NULL;

    for (int i = 0; i < output_count; i++) {
        output_cookie = xcb_randr_get_output_info(connection, outputs[i],
               resources->timestamp);
        output = xcb_randr_get_output_info_reply(connection, output_cookie, &error);
        if (error != NULL) {
            log_error(error, "unable to get output info of %d: ", i);
            free(error);
            continue;
        }

        name = (char*) xcb_randr_get_output_info_name(output);
        name_len = xcb_randr_get_output_info_name_length(output);

        if (output->connection != XCB_RANDR_CONNECTION_CONNECTED) {
            LOG("ignored output: '%.*s': not connected\n", name_len, name);
            continue;
        }

        if (output->crtc == XCB_NONE) {
            LOG("ignored output: '%.*s': no crtc\n", name_len, name);
            continue;
        }

        crtc_cookie = xcb_randr_get_crtc_info(connection, output->crtc,
                resources->timestamp);
        crtc = xcb_randr_get_crtc_info_reply(connection, crtc_cookie, &error);
        if (crtc == NULL) {
            log_error(error, "output: '%.*s' gave a NULL crtc?? ", name_len, name);
            free(error);
            continue;
        }

        if (first_monitor == NULL) {
            first_monitor = create_monitor(name, name_len);
            last_monitor = first_monitor;
        } else {
            last_monitor->next = create_monitor(name, name_len);
            last_monitor->next->prev = last_monitor;
            last_monitor = last_monitor->next;
        }

        last_monitor->primary = primary_output == outputs[i];

        last_monitor->position.x = crtc->x;
        last_monitor->position.y = crtc->y;
        last_monitor->size.width = crtc->width;
        last_monitor->size.height = crtc->height;
    }
    return first_monitor;
}

/* Updates the struts of all monitors and then correctly sizes the frame. */
void reconfigure_monitor_frame_sizes(void)
{
    Monitor *monitor;

    /* reset all extents */
    for (monitor = screen->monitor; monitor != NULL;
            monitor = monitor->next) {
        monitor->struts.left = 0;
        monitor->struts.top = 0;
        monitor->struts.right = 0;
        monitor->struts.bottom = 0;
    }

    /* work out the new extents based on the window defined extents */
    for (Window *window = first_window; window != NULL;
            window = window->next) {
        if (!window->state.is_visible ||
                is_strut_empty(&window->properties.struts)) {
            continue;
        }
        monitor = get_monitor_from_rectangle(window->position.x,
                window->position.y, window->size.width, window->size.height);
        monitor->struts.left += window->properties.struts.left;
        monitor->struts.top += window->properties.struts.top;
        monitor->struts.right += window->properties.struts.right;
        monitor->struts.bottom += window->properties.struts.bottom;
    }

    /* resize all frames to their according size */
    for (monitor = screen->monitor; monitor != NULL;
            monitor = monitor->next) {
        resize_frame(monitor->frame,
                monitor->position.x + monitor->struts.left,
                monitor->position.y + monitor->struts.top,
                monitor->size.width - monitor->struts.right -
                    monitor->struts.left,
                monitor->size.height - monitor->struts.bottom -
                    monitor->struts.top);
    }
}

/* Merges given monitor linked list into the screen.
 *
 * The main purpose of this function is to essentially make the linked in screen
 * be @monitors, but it is not enough to say: `screen->monitor = monitors`.
 *
 * The current rule is to keep monitors from the source and delete monitors no
 * longer in the list.
 */
void merge_monitors(Monitor *monitors)
{
    Monitor *named_monitor, *next_monitor, *other;

    if (monitors == NULL) {
        monitors = create_monitor("#Virtual", (uint32_t) -1);
        monitors->size.width = screen->xcb_screen->width_in_pixels;
        monitors->size.height = screen->xcb_screen->height_in_pixels;
    }

    /* copy frames from the old monitors to the new ones with same name */
    for (Monitor *monitor = monitors; monitor != NULL;
            monitor = monitor->next) {
        named_monitor = get_monitor_by_name(screen->monitor,
                monitor->name, strlen(monitor->name));
        if (named_monitor != NULL) {
            free(monitor->frame);
            monitor->frame = named_monitor->frame;
            named_monitor->frame = NULL;
        } else {
            monitor->is_free = 1;
        }
    }

    /* for the remaining monitors try to find any monitors to take the frames */
    for (Monitor *monitor = screen->monitor; monitor != NULL;
            monitor = next_monitor) {
        next_monitor = monitor->next;
        if (monitor->frame != NULL) {
            /* find a free monitor */
            for (other = monitors; other != NULL; other = other->next) {
                if (!other->is_free) {
                    continue;
                }

                free(other->frame);
                other->frame = monitor->frame;
                monitor->frame = NULL;
                break;
            }

            /* abandon the frame if there is no monitor that can take it */
            if (other == NULL) {
                if (focus_frame == monitor->frame) {
                    focus_frame = NULL;
                }
                abandon_frame(monitor->frame);
            }
        }
        free(monitor->name);
        free(monitor);
    }

    screen->monitor = monitors;

    reconfigure_monitor_frame_sizes();

    if (focus_frame == NULL) {
        focus_frame = get_primary_monitor()->frame;
    }
}
