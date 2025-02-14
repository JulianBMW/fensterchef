#include <stdlib.h>

#include "event.h"
#include "fensterchef.h"
#include "frame.h"
#include "keybind.h"
#include "keymap.h"
#include "log.h"
#include "root_properties.h"
#include "screen.h"
#include "xalloc.h"

/* FENSTERCHEF
 *
 * First, fensterchef is initialized, see init_fensterchef() for details and the
 * keyboard is setup (see init_keymap()).
 *
 * Then the main event loop runs, waiting for every xcb event and letting them
 * be handled by handle_event().
 */
int main(void)
{
    int screen_number;
    xcb_generic_event_t *event;

    if (init_fensterchef(&screen_number) != 0 ||
            init_keymap() != 0 ||
            init_screen(screen_number) != 0) {
        quit_fensterchef(EXIT_FAILURE);
    }

    init_keybinds();

    init_monitors();

    log_screen();

    synchronize_all_root_properties();

    focus_frame = get_primary_monitor()->frame;

    is_fensterchef_running = true;
    while (is_fensterchef_running) {
        if (xcb_connection_has_error(connection) > 0) {
            quit_fensterchef(EXIT_FAILURE);
        }
        event = xcb_wait_for_event(connection);
        if (event != NULL) {
            handle_event(event);
            free(event);
            xcb_flush(connection);
        }
    }

    quit_fensterchef(EXIT_SUCCESS);
}
