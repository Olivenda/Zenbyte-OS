/* window.h -- Zenbite Windowing System API
 *
 * Provides functions for creating and managing windows in the graphical
 * desktop. Each window has its own buffer and can be drawn to independently.
 *
 * Include this in .ZBX programs: #include "window.h"
 *
 * Example:
 *   int win = win_create(10, 10, 40, 15, "My Window", 0x1F);
 *   win_draw_text(win, 2, 2, 0x0F, "Hello World!");
 *   win_present(win);
 */
#ifndef ZENBITE_WINDOW_H
#define ZENBITE_WINDOW_H

/* ── Window Constants ──────────────────────────────────────────────── */

#define WIN_MAX_WINDOWS     16    /* Maximum concurrent windows */
#define WIN_MAX_TITLE       32    /* Maximum title length */
#define WIN_MIN_WIDTH       5
#define WIN_MIN_HEIGHT      3
#define WIN_MAX_WIDTH       80
#define WIN_MAX_HEIGHT      50

/* ── Window Flags ──────────────────────────────────────────────────── */

#define WIN_FLAG_NONE       0x00
#define WIN_FLAG_TITLEBAR   0x01  /* Has title bar */
#define WIN_FLAG_CLOSE_BTN  0x02  /* Has close button */
#define WIN_FLAG_RESIZABLE  0x04  /* Can be resized */
#define WIN_FLAG_MOVABLE    0x08  /* Can be moved */
#define WIN_FLAG_SHADOW     0x10  /* Has drop shadow */
#define WIN_FLAG_MODAL      0x20  /* Modal window (blocks input) */
#define WIN_FLAG_VISIBLE    0x40  /* Window is visible */
#define WIN_FLAG_DIRTY      0x80  /* Window needs redraw */

/* ── Window Events ─────────────────────────────────────────────────── */

#define WIN_EVT_NONE        0
#define WIN_EVT_CLOSE       1     /* Close button clicked */
#define WIN_EVT_MOVE        2     /* Window moved */
#define WIN_EVT_RESIZE      3     /* Window resized */
#define WIN_EVT_MOUSE       4     /* Mouse event in window */
#define WIN_EVT_KEY         5     /* Key event in window */
#define WIN_EVT_FOCUS       6     /* Window gained focus */
#define WIN_EVT_BLUR        7     /* Window lost focus */
#define WIN_EVT_PAINT       8     /* Window needs repainting */

/* ── Mouse Buttons ─────────────────────────────────────────────────── */

#define WIN_MOUSE_LEFT      0x01
#define WIN_MOUSE_RIGHT     0x02
#define WIN_MOUSE_MIDDLE    0x04

/* ── Text Alignment ────────────────────────────────────────────────── */

#define WIN_ALIGN_LEFT      0
#define WIN_ALIGN_CENTER    1
#define WIN_ALIGN_RIGHT     2

/* ── Widget Types ──────────────────────────────────────────────────── */

#define WIN_WIDGET_NONE     0
#define WIN_WIDGET_BUTTON   1
#define WIN_WIDGET_LABEL    2
#define WIN_WIDGET_TEXTBOX  3
#define WIN_WIDGET_CHECKBOX 4
#define WIN_WIDGET_RADIO    5
#define WIN_WIDGET_SLIDER   6
#define WIN_WIDGET_LISTBOX  7
#define WIN_WIDGET_SCROLLBAR 8
#define WIN_WIDGET_MENU     9
#define WIN_WIDGET_STATUS   10

/* ── Window Structure ──────────────────────────────────────────────── */

typedef struct {
    int id;                     /* Window ID (0 = invalid) */
    int row, col;               /* Position (row, col) */
    int width, height;          /* Dimensions */
    int inner_row, inner_col;   /* Inner content area start */
    int inner_width, inner_height;
    int flags;                  /* WIN_FLAG_* */
    int color;                  /* Default color attribute */
    int title_color;            /* Title bar color */
    int bg_color;               /* Background color */
    char title[WIN_MAX_TITLE];  /* Window title */
    int z_order;                /* Z-order (higher = on top) */
    int focused;                /* Has input focus */
    int widget_count;           /* Number of widgets */
    int widgets[32];            /* Widget IDs */
} win_info_t;

/* ── Widget Structure ──────────────────────────────────────────────── */

typedef struct {
    int id;                     /* Widget ID */
    int win_id;                 /* Parent window ID */
    int type;                   /* WIN_WIDGET_* */
    int row, col;               /* Position within window */
    int width, height;          /* Widget dimensions */
    int color;                  /* Color attribute */
    int state;                  /* Widget state (checked, selected, etc.) */
    int value;                  /* Numeric value */
    char text[64];              /* Widget text */
    int hotkey;                 /* Keyboard shortcut */
    int enabled;                /* Widget enabled */
} win_widget_t;

/* ══════════════════════════════════════════════════════════════════════
 * WINDOW MANAGEMENT FUNCTIONS
 * ══════════════════════════════════════════════════════════════════════ */

/* ── Window Creation / Destruction ─────────────────────────────────── */

/* Create a new window. Returns window ID (0 on failure).
 * row, col: position; width, height: dimensions
 * title: window title string; color: default color */
int win_create(int row, int col, int width, int height,
               const char *title, int color);

/* Destroy a window and free its resources */
void win_destroy(int win_id);

/* Get window info structure */
win_info_t *win_get_info(int win_id);

/* ── Window Visibility ─────────────────────────────────────────────── */

/* Show a hidden window */
void win_show(int win_id);

/* Hide a window (still exists, not drawn) */
void win_hide(int win_id);

/* Check if window is visible */
int win_is_visible(int win_id);

/* ── Window Position / Size ────────────────────────────────────────── */

/* Move window to new position */
void win_move(int win_id, int row, int col);

/* Resize window */
void win_resize(int win_id, int width, int height);

/* Get window bounds */
void win_get_bounds(int win_id, int *row, int *col, int *width, int *height);

/* Get inner content area bounds */
void win_get_inner_bounds(int win_id, int *row, int *col,
                          int *width, int *height);

/* ── Window Z-Order ────────────────────────────────────────────────── */

/* Bring window to front */
void win_raise(int win_id);

/* Send window to back */
void win_lower(int win_id);

/* Set window Z-order */
void win_set_zorder(int win_id, int z);

/* ── Window Focus ──────────────────────────────────────────────────── */

/* Give input focus to window */
void win_focus(int win_id);

/* Remove focus from window */
void win_blur(int win_id);

/* Check if window has focus */
int win_is_focused(int win_id);

/* Get currently focused window */
int win_get_focused(void);

/* ── Window Drawing ────────────────────────────────────────────────── */

/* Clear window with color */
void win_clear(int win_id, int color);

/* Draw a single character at position within window */
void win_putc(int win_id, int row, int col, char ch, int color);

/* Draw string at position within window */
void win_puts(int win_id, int row, int col, int color, const char *s);

/* Draw text with alignment (LEFT, CENTER, RIGHT) */
void win_puts_aligned(int win_id, int row, int col, int width,
                      int color, int align, const char *s);

/* Draw a horizontal line */
void win_hline(int win_id, int row, int col, int width, char ch, int color);

/* Draw a vertical line */
void win_vline(int win_id, int row, int col, int height, char ch, int color);

/* Draw a box outline */
void win_box(int win_id, int row, int col, int width, int height,
             char border, int color);

/* Draw a filled rectangle */
void win_fill(int win_id, int row, int col, int width, int height,
              char fill, int color);

/* ── Window Title ──────────────────────────────────────────────────── */

/* Set window title */
void win_set_title(int win_id, const char *title);

/* Get window title */
const char *win_get_title(int win_id);

/* ── Window Colors ─────────────────────────────────────────────────── */

/* Set window default color */
void win_set_color(int win_id, int color);

/* Set title bar color */
void win_set_title_color(int win_id, int color);

/* Set background color */
void win_set_bg_color(int win_id, int color);

/* ── Window Presentation ───────────────────────────────────────────── */

/* Mark window as needing redraw */
void win_invalidate(int win_id);

/* Present window to screen (draw it) */
void win_present(int win_id);

/* Present all visible windows */
void win_present_all(void);

/* ── Window Events ─────────────────────────────────────────────────── */

/* Get next event for window (returns WIN_EVT_*) */
int win_get_event(int win_id);

/* Poll for event (non-blocking, returns WIN_EVT_NONE if none) */
int win_poll_event(int win_id);

/* Wait for event (blocking) */
int win_wait_event(int win_id);

/* Get event data (mouse position, key pressed, etc.) */
void win_get_event_data(int *data1, int *data2);

/* ══════════════════════════════════════════════════════════════════════
 * WIDGET FUNCTIONS
 * ══════════════════════════════════════════════════════════════════════ */

/* ── Button ────────────────────────────────────────────────────────── */

/* Create a button. Returns widget ID. */
int win_create_button(int win_id, int row, int col, int width,
                      const char *text, int color, int hotkey);

/* Check if button was clicked */
int win_button_clicked(int widget_id);

/* ── Label ─────────────────────────────────────────────────────────── */

/* Create a label. Returns widget ID. */
int win_create_label(int win_id, int row, int col, int width,
                     const char *text, int color, int align);

/* Update label text */
void win_label_set_text(int widget_id, const char *text);

/* ── Text Box ──────────────────────────────────────────────────────── */

/* Create a text input box. Returns widget ID. */
int win_create_textbox(int win_id, int row, int col, int width,
                       int max_len, int color);

/* Get text box content */
const char *win_textbox_get_text(int widget_id);

/* Set text box content */
void win_textbox_set_text(int widget_id, const char *text);

/* ── Checkbox ──────────────────────────────────────────────────────── */

/* Create a checkbox. Returns widget ID. */
int win_create_checkbox(int win_id, int row, int col,
                        const char *text, int color, int checked);

/* Get checkbox state */
int win_checkbox_get_state(int widget_id);

/* Set checkbox state */
void win_checkbox_set_state(int widget_id, int checked);

/* ── Radio Button ──────────────────────────────────────────────────── */

/* Create a radio button. Returns widget ID. */
int win_create_radio(int win_id, int row, int col,
                     const char *text, int color, int group);

/* Get radio button state (1 if selected) */
int win_radio_get_state(int widget_id);

/* Select radio button (deselects others in group) */
void win_radio_select(int widget_id);

/* ── Slider ────────────────────────────────────────────────────────── */

/* Create a slider. Returns widget ID. */
int win_create_slider(int win_id, int row, int col, int width,
                      int min_val, int max_val, int color);

/* Get slider value */
int win_slider_get_value(int widget_id);

/* Set slider value */
void win_slider_set_value(int widget_id, int value);

/* ── List Box ──────────────────────────────────────────────────────── */

/* Create a list box. Returns widget ID. */
int win_create_listbox(int win_id, int row, int col, int width,
                       int height, int color);

/* Add item to list box */
void win_listbox_add_item(int widget_id, const char *item);

/* Get selected index (-1 if none) */
int win_listbox_get_selected(int widget_id);

/* Set selected index */
void win_listbox_set_selected(int widget_id, int index);

/* Clear all items */
void win_listbox_clear(int widget_id);

/* ── Scrollbar ─────────────────────────────────────────────────────── */

/* Create a scrollbar. Returns widget ID. */
int win_create_scrollbar(int win_id, int row, int col, int height,
                         int total, int visible, int color);

/* Get scrollbar position */
int win_scrollbar_get_position(int widget_id);

/* Set scrollbar position */
void win_scrollbar_set_position(int widget_id, int pos);

/* ── Menu ──────────────────────────────────────────────────────────── */

/* Create a menu bar. Returns widget ID. */
int win_create_menu(int win_id, int color);

/* Add menu item */
void win_menu_add_item(int widget_id, int parent, const char *text,
                       int id, int hotkey);

/* Get selected menu item ID (-1 if none) */
int win_menu_get_selected(int widget_id);

/* ── Status Bar ────────────────────────────────────────────────────── */

/* Create a status bar. Returns widget ID. */
int win_create_status(int win_id, int color);

/* Update status bar text */
void win_status_set_text(int widget_id, const char *text);

/* ══════════════════════════════════════════════════════════════════════
 * DIALOG FUNCTIONS
 * ══════════════════════════════════════════════════════════════════════ */

/* Show a message box (OK button) */
void win_msgbox(const char *title, const char *message, int color);

/* Show a confirmation dialog (Yes/No). Returns 1 for Yes, 0 for No. */
int win_confirm(const char *title, const char *message, int color);

/* Show an input dialog. Returns entered string (or NULL on cancel). */
const char *win_input(const char *title, const char *prompt,
                      int max_len, int color);

/* Show a file open dialog. Returns selected path (or NULL on cancel). */
const char *win_file_open(const char *title, const char *pattern);

/* Show a file save dialog. Returns entered path (or NULL on cancel). */
const char *win_file_save(const char *title, const char *default_name);

/* ══════════════════════════════════════════════════════════════════════
 * DESKTOP INTEGRATION
 * ══════════════════════════════════════════════════════════════════════ */

/* Initialize windowing system */
void win_init(void);

/* Shutdown windowing system */
void win_shutdown(void);

/* Get screen dimensions */
void win_get_screen_size(int *width, int *height);

/* Check if point is inside window */
int win_point_in_window(int win_id, int row, int col);

/* Check if windows overlap */
int win_overlaps(int win1, int win2);

/* Get window at position (top-most) */
int win_get_at_position(int row, int col);

#endif /* ZENBITE_WINDOW_H */
