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
 */
int ui_select_history(NcdMatch *matches, int *count, NcdMetadata *meta);

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

#ifdef __cplusplus
}
#endif

#endif /* NCD_UI_H */
