#include "fensterchef.h"
#include "keybind.h"
#include "keymap.h"

/* symbol translation table */
static xcb_key_symbols_t *key_symbols;

/* Initializes the keymap so the below functions can be used. */
int init_keymap(void)
{
    key_symbols = xcb_key_symbols_alloc(connection);
    if (key_symbols == NULL) {
        return 1;
    }
    return 0;
}

/* Refresh the keymap if a mapping notify event arrives. */
void refresh_keymap(xcb_mapping_notify_event_t *event)
{
    (void) xcb_refresh_keyboard_mapping(key_symbols, event);
    /* regrab all keys */
    init_keybinds();
}

/* Get a keysym from a keycode. */
xcb_keysym_t get_keysym(xcb_keycode_t keycode)
{
    return xcb_key_symbols_get_keysym(key_symbols, keycode, 0);
}

/* Get a list of keycodes from a keysym. */
xcb_keycode_t *get_keycodes(xcb_keysym_t keysym)
{
    return xcb_key_symbols_get_keycode(key_symbols, keysym);
}
