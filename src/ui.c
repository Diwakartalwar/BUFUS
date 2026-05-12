#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "ui.h"

/* ── Colour helpers ───────────────────────────────────────────────── */
#define COL_RESET  (FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE)
#define COL_CYAN   (FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY)
#define COL_YELLOW (FOREGROUND_RED  | FOREGROUND_GREEN | FOREGROUND_INTENSITY)
#define COL_GREEN  (FOREGROUND_GREEN | FOREGROUND_INTENSITY)
#define COL_RED    (FOREGROUND_RED   | FOREGROUND_INTENSITY)
#define COL_GREY   (FOREGROUND_INTENSITY)

static void set_color(WORD attr) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), attr);
}

/* ── Banner ───────────────────────────────────────────────────────── */
void ui_print_header(void) {
    /* Enable UTF-8 output so block chars render correctly */
    SetConsoleOutputCP(CP_UTF8);

    set_color(COL_CYAN);
    printf("\n");
    printf("  ██████╗ ██╗   ██╗███████╗██╗   ██╗███████╗\n");
    printf("  ██╔══██╗██║   ██║██╔════╝██║   ██║██╔════╝\n");
    printf("  ██████╔╝██║   ██║█████╗  ██║   ██║███████╗\n");
    printf("  ██╔══██╗██║   ██║██╔══╝  ██║   ██║╚════██║\n");
    printf("  ██████╔╝╚██████╔╝██║     ╚██████╔╝███████║\n");
    printf("  ╚═════╝  ╚═════╝ ╚═╝      ╚═════╝ ╚══════╝\n");
    set_color(COL_RESET);
    printf("  Blazing USB Flash Utility System  v0.1.0\n");
    set_color(COL_GREY);
    printf("  ─────────────────────────────────────────\n\n");
    set_color(COL_RESET);
}

/* ── Drive table ──────────────────────────────────────────────────── */
void ui_list_devices(const device_list_t *list) {
    if (list->count == 0) {
        set_color(COL_YELLOW);
        printf("  [!] No physical drives detected.\n\n");
        set_color(COL_RESET);
        return;
    }

    set_color(COL_GREY);
    printf("  %-5s  %-34s  %-10s  %s\n", "IDX", "MODEL", "SIZE", "TYPE");
    printf("  ─────────────────────────────────────────────────────────\n");
    set_color(COL_RESET);

    for (int i = 0; i < list->count; i++) {
        const device_info_t *d = &list->drives[i];
        double gb = (double)d->size_bytes / (1024.0 * 1024.0 * 1024.0);
        const char *type = d->is_removable ? "USB/Removable" : "Fixed";

        set_color(d->is_removable ? COL_GREEN : COL_RESET);
        printf("  [%d]    %-34s  %7.2f GB  %s\n",
               d->index,
               d->model[0] ? d->model : "(unknown)",
               gb,
               type);
    }
    set_color(COL_RESET);
    printf("\n");
}

/* ── Confirmation prompt ──────────────────────────────────────────── */
bool ui_confirm(const char *prompt) {
    set_color(COL_YELLOW);
    printf("  %s [y/N]: ", prompt);
    set_color(COL_RESET);
    fflush(stdout);

    char first = 0;
    int ch;
    while ((ch = getchar()) != '\n' && ch != EOF) {
        if (first == 0) first = (char)ch;
    }
    return (first == 'y' || first == 'Y');
}

/* ── Progress bar ─────────────────────────────────────────────────── */
#define BAR_W 42

void ui_progress(uint64_t done, uint64_t total, double speed_mbs, void *userdata) {
    (void)userdata;

    double pct    = (total > 0) ? (double)done / (double)total * 100.0 : 0.0;
    int    filled = (int)(pct / 100.0 * BAR_W);
    double done_mib  = (double)done  / (1024.0 * 1024.0);
    double total_mib = (double)total / (1024.0 * 1024.0);

    printf("\r  [");
    set_color(COL_GREEN);
    for (int i = 0; i < filled; i++)        printf("\xe2\x96\x88"); /* █ */
    set_color(COL_GREY);
    for (int i = filled; i < BAR_W; i++)    printf("\xe2\x96\x91"); /* ░ */
    set_color(COL_RESET);
    printf("]  %5.1f%%  %6.1f MiB/s  %.0f/%.0f MiB",
           pct, speed_mbs, done_mib, total_mib);
    fflush(stdout);

    if (done >= total) printf("\n");
}

/* ── Terminal status messages ─────────────────────────────────────── */
void ui_print_success(void) {
    set_color(COL_GREEN);
    printf("\n  \xe2\x9c\x93 Done! Drive is ready.\n\n");
    set_color(COL_RESET);
}

void ui_print_error(const char *msg) {
    set_color(COL_RED);
    fprintf(stderr, "\n  \xe2\x9c\x97 Error: %s\n\n", msg);
    set_color(COL_RESET);
}