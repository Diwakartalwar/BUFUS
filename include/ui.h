/**
 * @file ui.h
 * @brief User interface and progress display
 * @author BUFUS Development Team
 * @date 2024
 */

#ifndef UI_H
#define UI_H

#include <stdbool.h>
#include <stdint.h>
#include "device.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ───────────────────────────────────────────────────────────────── */
/* Display functions                                                  */
/* ───────────────────────────────────────────────────────────────── */

/**
 * Print the BUFUS header banner.
 *
 * Displays the ASCII art logo and version. Enables UTF-8 console output
 * for proper character rendering.
 */
void ui_print_header(void);

/**
 * Display a formatted table of physical devices.
 *
 * Shows index, model, capacity, and type (fixed/USB) for each drive.
 * Color-codes USB drives in green.
 *
 * @param list Pointer to device_list_t (non-NULL)
 */
void ui_list_devices(const device_list_t *list);

/**
 * Print a confirmation prompt and get user response.
 *
 * Displays a yellow prompt and waits for user input. Returns true only
 * if the first character entered is 'y' or 'Y'.
 *
 * @param prompt Prompt text (non-NULL)
 * @return true for 'y'/'Y', false for anything else or EOF
 */
bool ui_confirm(const char *prompt);

/**
 * Update progress bar display.
 *
 * Shows a formatted progress bar with percentage, speed, and bytes.
 * This is a progress callback suitable for passing to io_write_image
 * or verify_image.
 *
 * @param done      Bytes completed
 * @param total     Total bytes
 * @param speed_mbs Transfer speed in MiB/s
 * @param userdata  Unused (NULL)
 */
void ui_progress(uint64_t done, uint64_t total, double speed_mbs, void *userdata);

/* ───────────────────────────────────────────────────────────────── */
/* Status messages                                                    */
/* ───────────────────────────────────────────────────────────────── */

/**
 * Print success message.
 *
 * Displays a green checkmark and "Done!" message.
 */
void ui_print_success(void);

/**
 * Print error message.
 *
 * Displays a red X and error text to stderr.
 *
 * @param msg Error message text
 */
void ui_print_error(const char *msg);

#ifdef __cplusplus
}
#endif

#endif /* UI_H */
