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
 * Interactive filesystem navigator used by "ncd .".
 *
 * start_path must be an absolute path.
 * On Enter, writes the selected path to out_path and returns true.
 * On Esc/cancel or error, returns false.
 */
bool ui_navigate_directory(const char *start_path,
                           char       *out_path,
                           size_t      out_path_size);

#ifdef __cplusplus
}
#endif

#endif /* NCD_UI_H */
