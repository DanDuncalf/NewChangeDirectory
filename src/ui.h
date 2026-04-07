/*
 * ui.h  --  Interactive ASCII terminal selection UI for NCD
 *
 * When multiple directories match a search, present a scrollable list
 * drawn directly on the console (stderr / CONOUT$) so that stdout can
 * remain clean for scripting.
 *
 * Keys
 * ----
 *   Up / Down arrow  -- move highlight
 *   Page Up / Down   -- scroll a page at a time
 *   Home / End       -- jump to first / last item
 *   Enter            -- confirm selection
 *   Escape / q       -- cancel (no directory change)
 */

#ifndef NCD_UI_H
#define NCD_UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ncd.h"
#include "matcher.h"

/*
 * Present the list of matches and let the user choose one.
 *
 * Returns the index into matches[] of the chosen entry,
 * or -1 if the user cancelled.
 */
int ui_select_match(const NcdMatch *matches, int count,
                    const char *search_str);

/*
 * Extended selector:
 *   >=0  => index into matches[]
 *   -1   => cancelled
 *   -2   => custom path selected in navigator (written to out_path)
 */
int ui_select_match_ex(const NcdMatch *matches, int count,
                       const char *search_str,
                       char       *out_path,
                       size_t      out_path_size);

/*
 * History browser with Delete key support.
 * Shows history entries in a selectable list.
 * Press Delete to remove the highlighted entry.
 * Returns the index of the selected entry, or -1 if cancelled.
 * The matches array and count may be modified if entries are deleted.
 * 
 * The delete_callback (if provided) is called when user presses Delete.
 * It should return true on success, false on failure.
 * If NULL, metadata is modified directly (legacy behavior).
 */
typedef bool (*ui_history_delete_cb)(int index, const char *path, void *user_data);

int ui_select_history(NcdMatch *matches, int *count, NcdMetadata *meta,
                      ui_history_delete_cb delete_callback, void *user_data);

/*
 * Interactive filesystem navigator used by "ncd .".
 *
 * start_path must be an absolute path.
 * On Enter, writes the selected path to out_path and returns true.
 * On Esc/cancel or error, returns false.
 */
bool ui_navigate_directory(const char *start_path,
                           char       *out_path,
                           size_t      out_path_size);

/* --------------------------------------------------------- version update  */

/*
 * Result codes for ui_select_drives_for_update
 */
#define UI_UPDATE_ALL    0   /* User selected "Update All" */
#define UI_UPDATE_NONE  -1   /* User cancelled (Esc/q) */
#define UI_UPDATE_SOME   1   /* User selected specific drives (marked in selected[] array) */

/*
 * Present a TUI for selecting which outdated drives to rescan.
 *
 * drives[]      - Array of drive letters needing update
 * versions[]    - Array of version numbers found (parallel to drives)
 * count         - Number of entries in drives[]/versions[]
 * selected[]    - Output: bool array indicating which drives user selected
 *                   (only valid if return value is UI_UPDATE_SOME)
 * max_width     - Maximum width of the dialog box
 *
 * Returns:
 *   UI_UPDATE_ALL  - User wants to rescan ALL drives
 *   UI_UPDATE_NONE - User cancelled (no drives to rescan)
 *   UI_UPDATE_SOME - User selected specific drives (check selected[] array)
 *
 * The selected[] array must be allocated by the caller with at least 'count' entries.
 * On UI_UPDATE_ALL, all entries in selected[] are set to true.
 * On UI_UPDATE_NONE, all entries in selected[] are set to false.
 */
int ui_select_drives_for_update(const char *drives,
                                const uint16_t *versions,
                                int count,
                                bool *selected,
                                int max_width);

/* --------------------------------------------------------- configuration  */

/*
 * Present a TUI for editing configuration options.
 *
 * meta - Pointer to current metadata (config will be modified)
 *
 * Returns:
 *   true  - User saved changes
 *   false - User cancelled
 */
bool ui_edit_config(NcdMetadata *meta);

/*
 * Present a TUI for managing exclusion list.
 *
 * meta - Pointer to metadata containing exclusions (will be modified)
 *
 * Returns:
 *   true  - User saved changes
 *   false - User cancelled
 */
bool ui_edit_exclusions(NcdMetadata *meta);

/* ========================================================== I/O backend  */

/*
 * UI I/O operations abstraction.
 * The default implementation uses the platform console.
 * Tests can install an in-memory backend for headless validation.
 */
typedef struct UiIoOps {
    void (*open_console)(void);
    void (*close_console)(void);
    void (*write)(const char *s);
    void (*write_at)(int row, int col, const char *s);
    void (*write_padded)(const char *s, int width);
    void (*cursor_pos)(int row, int col);
    void (*hide_cursor)(void);
    void (*show_cursor)(void);
    void (*clear_area)(int top_row, int rows, int width);
    void (*get_size)(int *cols, int *rows, int *cur_row);
    int  (*read_key)(void);
    bool ansi_enabled;
} UiIoOps;

/*
 * Install a custom I/O backend.
 * Pass NULL to restore the default platform backend.
 */
void ui_set_io_backend(UiIoOps *ops);

/*
 * Get the current I/O backend.
 */
UiIoOps *ui_get_io_backend(void);

/*
 * Scripted key injection for automated TUI testing.
 *
 * key_script: comma-separated tokens such as:
 *   UP, DOWN, LEFT, RIGHT, PGUP, PGDN, HOME, END
 *   ENTER, ESC, TAB, BACKSPACE, DELETE
 *   TEXT:hello
 *
 * Returns 0 on success, -1 on parse error.
 */
int ui_inject_keys(const char *key_script);

/*
 * Load scripted keys from a file.
 */
int ui_inject_keys_from_file(const char *path);

/*
 * Clear any injected keys.
 */
void ui_clear_injected_keys(void);

/*
 * Check if injected key queue is empty.
 */
bool ui_injected_keys_empty(void);

/* ======================================== test backend (NCD_TEST_BUILD) */
#ifdef NCD_TEST_BUILD

/*
 * Frame snapshot for test assertions.
 * Allocated only when test backend is active.
 */
typedef struct UiSnapshot {
    char **rows;          /* Array of row strings */
    int    row_count;     /* Number of rows captured */
    int    width;         /* Grid width */
    int    selected_row;  /* Highlighted row index (-1 if none) */
    char   filter[64];    /* Current filter text */
    int    scroll_top;    /* First visible item index */
    bool   has_header;    /* Whether header/title was detected */
} UiSnapshot;

/*
 * Capture a snapshot of the current test backend frame.
 * Returns NULL if test backend is not active.
 * Caller must free with ui_snapshot_free().
 */
UiSnapshot *ui_snapshot_capture(void);

/*
 * Free a snapshot and all associated memory.
 */
void ui_snapshot_free(UiSnapshot *snap);

/*
 * Test backend: create an in-memory grid backend.
 * cols/rows: grid dimensions (e.g., 80, 25)
 * Returns a backend that records all output to an internal character grid.
 * The backend pointer remains valid until ui_set_io_backend(NULL) is called.
 */
UiIoOps *ui_create_test_backend(int cols, int rows);

/*
 * Destroy a test backend created with ui_create_test_backend().
 */
void ui_destroy_test_backend(UiIoOps *ops);

/*
 * Get the text content of a specific row from the test backend.
 * Returns a pointer to internal storage (do not free).
 * Returns empty string if out of bounds.
 */
const char *ui_test_backend_row(int row);

/*
 * Find the first row containing the given substring in the test backend.
 * Returns row index or -1 if not found.
 */
int ui_test_backend_find_row(const char *substring);

#endif /* NCD_TEST_BUILD */

#ifdef __cplusplus
}
#endif

#endif /* NCD_UI_H */
