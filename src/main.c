#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "bufus.h"
#include "logger.h"
#include "ui.h"
#include "device.h"
#include "disk.h"
#include "io.h"
#include "verify.h"
#include "benchmark.h"
#include "image_probe.h"

/* ── Error string table ───────────────────────────────────────────── */

const char *bufus_err_str(bufus_err_t e) {
    switch (e) {
        case BUFUS_OK:            return "success";
        case BUFUS_ERR_ARGS:      return "invalid arguments";
        case BUFUS_ERR_PRIVILEGE: return "access denied — run as Administrator";
        case BUFUS_ERR_NOT_FOUND: return "device not found";
        case BUFUS_ERR_OPEN:      return "cannot open device or file";
        case BUFUS_ERR_LOCK:      return "cannot lock volume";
        case BUFUS_ERR_IO_READ:   return "read error";
        case BUFUS_ERR_IO_WRITE:  return "write error";
        case BUFUS_ERR_VERIFY:    return "verification failed — data mismatch";
        case BUFUS_ERR_NOMEM:     return "out of memory";
        case BUFUS_ERR_WIN32:     return "unexpected Win32 API error";
        default:                  return "unknown error";
    }
}

/* ── Privilege check ──────────────────────────────────────────────── */

bool bufus_is_elevated(void) {
    HANDLE token = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;

    TOKEN_ELEVATION elev;
    DWORD size = sizeof(elev);
    BOOL  ok   = GetTokenInformation(token, TokenElevation,
                                     &elev, size, &size);
    CloseHandle(token);
    return ok && (elev.TokenIsElevated != 0);
}

/* ── CLI help ─────────────────────────────────────────────────────── */

static void usage(void) {
    printf(
        "Usage:\n"
        "  bufus.exe -s <image.iso> -d <index>  [options]\n"
        "  bufus.exe --list\n"
        "  bufus.exe --benchmark -d <index>\n"
        "\n"
        "Options:\n"
        "  -s <path>       Source ISO / IMG file\n"
        "  -d <N>          Target PhysicalDrive index (see --list)\n"
        "  --list          List all detected drives and exit\n"
        "  --verify        Verify written data after imaging\n"
        "  --force         Skip the destruction warning prompt\n"
        "  --block <MiB>   I/O block size in MiB (default 4)\n"
        "  --log <file>    Write full log to <file>\n"
        "  --verbose       Enable debug-level log output\n"
        "  --benchmark     Measure drive read/write speed (256 MiB test)\n"
        "  -h, --help      Show this help\n"
        "\n"
        "Examples:\n"
        "  bufus.exe --list\n"
        "  bufus.exe -s ubuntu.iso -d 2 --verify\n"
        "  bufus.exe --benchmark -d 2\n"
        "\n"
    );
}

/* ── Argument parser ──────────────────────────────────────────────── */

#define DRIVE_LIST_SENTINEL (-2)

static int parse_int_arg(const char *s, int *out) {
    char *end = NULL;
    long v;

    if (!s || !*s || !out) return 0;
    errno = 0;
    v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0') return 0;
    if (v < -2147483647L - 1L || v > 2147483647L) return 0;

    *out = (int)v;
    return 1;
}

static bufus_cfg_t parse_args(int argc, char **argv) {
    bufus_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.drive_index = -1;
    cfg.block_size  = BUFUS_DEFAULT_BLOCK;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            strncpy(cfg.source, argv[++i], BUFUS_MAX_PATH_LEN - 1);
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            int drive_idx = -1;
            if (!parse_int_arg(argv[++i], &drive_idx) || drive_idx < 0) {
                fprintf(stderr, "  Invalid drive index for -d: %s\n\n", argv[i]);
                usage();
                exit(1);
            }
            cfg.drive_index = drive_idx;
        } else if (strcmp(argv[i], "--list") == 0) {
            cfg.drive_index = DRIVE_LIST_SENTINEL;
        } else if (strcmp(argv[i], "--verify") == 0) {
            cfg.verify = true;
        } else if (strcmp(argv[i], "--force") == 0) {
            cfg.force = true;
        } else if (strcmp(argv[i], "--verbose") == 0) {
            cfg.verbose = true;
        } else if (strcmp(argv[i], "--benchmark") == 0) {
            cfg.benchmark = true;
        } else if (strcmp(argv[i], "--block") == 0 && i + 1 < argc) {
            int mib = 0;
            if (!parse_int_arg(argv[++i], &mib) || mib <= 0 || mib > 1024) {
                fprintf(stderr, "  Invalid block size for --block: %s (expected 1..1024 MiB)\n\n", argv[i]);
                usage();
                exit(1);
            }
            cfg.block_size = (uint32_t)mib * 1024u * 1024u;
        } else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            strncpy(cfg.log_file, argv[++i], BUFUS_MAX_PATH_LEN - 1);
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            usage();
            exit(0);
        } else {
            fprintf(stderr, "  Unknown option: %s\n\n", argv[i]);
            usage();
            exit(1);
        }
    }
    return cfg;
}

/* ── Benchmark flow ───────────────────────────────────────────────── */

static int run_benchmark(const bufus_cfg_t *cfg,
                         const device_info_t *dinfo) {
    printf("  Running benchmark on PhysicalDrive%d...\n\n",
           cfg->drive_index);

    if (!cfg->force) {
        if (!ui_confirm("WARNING: This will overwrite ~256 MiB of data. Continue?")) {
            printf("  Aborted.\n\n");
            return 0;
        }
    }

    HANDLE h;
    bufus_err_t rc = device_open(cfg->drive_index, &h);
    if (rc != BUFUS_OK) { ui_print_error(bufus_err_str(rc)); return 1; }
    rc = device_lock(h, cfg->drive_index);
    if (rc != BUFUS_OK) {
        ui_print_error(bufus_err_str(rc));
        device_close(h);
        return 1;
    }

    bench_result_t br;
    memset(&br, 0, sizeof(br));
    const uint64_t TEST_BYTES = 256ull * 1024 * 1024;

    rc = benchmark_run(h, cfg->block_size,
                       dinfo->sector_size ? dinfo->sector_size : 512,
                       TEST_BYTES, &br);

    device_unlock(h);
    device_close(h);

    if (rc != BUFUS_OK) { ui_print_error(bufus_err_str(rc)); return 1; }

    printf("\n"
           "  \xe2\x94\x8c\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
           "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
           "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
           "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
           "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
           "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
           "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x90\n"
           "  \xe2\x94\x82  Benchmark Results                     \xe2\x94\x82\n"
           "  \xe2\x94\x82  Write : %8.1f MiB/s                \xe2\x94\x82\n"
           "  \xe2\x94\x82  Read  : %8.1f MiB/s                \xe2\x94\x82\n"
           "  \xe2\x94\x82  Tested: %7.0f MiB                  \xe2\x94\x82\n"
           "  \xe2\x94\x94\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
           "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
           "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
           "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
           "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
           "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80"
           "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x98\n\n",
           br.write_mbs, br.read_mbs,
           (double)br.bytes_tested / (1024.0 * 1024.0));
    return 0;
}

/* ── Write + optional verify flow ─────────────────────────────────── */

static int run_write(const bufus_cfg_t *cfg,
                     const device_info_t *dinfo) {
    HANDLE   src      = INVALID_HANDLE_VALUE;
    uint64_t src_size = 0;

    bufus_err_t rc = io_open_source(cfg->source, &src, &src_size);
    if (rc != BUFUS_OK) { ui_print_error(bufus_err_str(rc)); return 1; }

    printf("  Source : %s\n", cfg->source);
    printf("           %.2f MiB (%llu bytes)\n",
           (double)src_size / (1024.0 * 1024.0), src_size);
    printf("  Target : PhysicalDrive%d - %s\n",
           cfg->drive_index,
           dinfo->model[0] ? dinfo->model : "(unknown)");
    printf("           %.2f GB\n\n",
           (double)dinfo->size_bytes / (1024.0 * 1024.0 * 1024.0));

    if (src_size > dinfo->size_bytes) {
        CloseHandle(src);
        ui_print_error("Source image is larger than the target drive.");
        return 1;
    }

    image_probe_t probe;
    rc = image_probe(src, src_size, &probe);
    if (rc != BUFUS_OK) {
        CloseHandle(src);
        ui_print_error("Failed to inspect source image structure.");
        return 1;
    }

    printf("  Image  : %s\n", image_kind_str(probe.kind));
    printf("           MBR=%s  GPT=%s  ISO9660=%s  UEFIPathHint=%s\n\n",
           probe.has_mbr_signature ? "yes" : "no",
           probe.has_gpt_header ? "yes" : "no",
           probe.has_iso9660_pvd ? "yes" : "no",
           probe.has_uefi_boot_path_hint ? "yes" : "no");

    if (!image_probe_dd_safe(&probe) && !cfg->force) {
        CloseHandle(src);
        ui_print_error(
            "This image does not look like a raw USB-disk image.\n"
            "  Refusing raw write to avoid creating a non-bootable USB.\n"
            "  Re-run with --force only if you explicitly want raw ISO write.");
        return 1;
    }

    if (!cfg->force) {
        char prompt[300];
        snprintf(prompt, sizeof(prompt),
                 "ALL data on PhysicalDrive%d will be permanently erased. Continue?",
                 cfg->drive_index);
        if (!ui_confirm(prompt)) {
            printf("\n  Aborted.\n\n");
            CloseHandle(src);
            return 0;
        }
    }

    HANDLE dev = INVALID_HANDLE_VALUE;
    rc = device_open(cfg->drive_index, &dev);
    if (rc != BUFUS_OK) {
        ui_print_error(bufus_err_str(rc));
        CloseHandle(src);
        return 1;
    }
    rc = device_lock(dev, cfg->drive_index);
    if (rc != BUFUS_OK) {
        ui_print_error(bufus_err_str(rc));
        device_close(dev);
        CloseHandle(src);
        return 1;
    }

    uint32_t sector = dinfo->sector_size ? dinfo->sector_size : 512;

    printf("  Sanitizing stale partition metadata...\n");
    rc = disk_sanitize_layout(dev, dinfo->size_bytes, src_size);
    if (rc != BUFUS_OK) {
        ui_print_error("Disk sanitization failed.");
        device_unlock(dev);
        device_close(dev);
        CloseHandle(src);
        return 1;
    }
    rc = disk_refresh_layout(dev);
    if (rc != BUFUS_OK) {
        ui_print_error("Disk layout refresh failed after sanitization.");
        device_unlock(dev);
        device_close(dev);
        CloseHandle(src);
        return 1;
    }

    printf("\n  Writing image...\n");
    io_params_t wp = {
        .src         = src,
        .dst         = dev,
        .src_size    = src_size,
        .block_size  = cfg->block_size,
        .sector_size = sector,
        .progress    = ui_progress,
        .userdata    = NULL,
    };
    rc = io_write_image(&wp);

    if (rc != BUFUS_OK) {
        ui_print_error(bufus_err_str(rc));
        device_unlock(dev);
        device_close(dev);
        CloseHandle(src);
        return 1;
    }

    device_unlock(dev);
    device_close(dev);

    if (cfg->verify) {
        rc = device_open(cfg->drive_index, &dev);
        if (rc != BUFUS_OK) {
            ui_print_error("Write succeeded, but verify reopen failed.");
            CloseHandle(src);
            return 1;
        }
        rc = device_lock(dev, cfg->drive_index);
        if (rc != BUFUS_OK) {
            ui_print_error("Write succeeded, but verify lock failed.");
            device_close(dev);
            CloseHandle(src);
            return 1;
        }

        printf("\n  Verifying...\n");
        verify_params_t vp = {
            .src         = src,
            .dst         = dev,
            .size        = src_size,
            .block_size  = cfg->block_size,
            .sector_size = sector,
            .progress    = ui_progress,
            .userdata    = NULL,
        };
        rc = verify_image(&vp);
        if (rc != BUFUS_OK) {
            ui_print_error(bufus_err_str(rc));
            device_unlock(dev);
            device_close(dev);
            CloseHandle(src);
            return 1;
        }

        device_unlock(dev);
        device_close(dev);
    }

    rc = device_open(cfg->drive_index, &dev);
    if (rc == BUFUS_OK) {
        disk_refresh_layout(dev);
        device_close(dev);
    }

    CloseHandle(src);
    ui_print_success();
    return 0;
}

/* ── Entry point ──────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    SetConsoleOutputCP(CP_UTF8);
    ui_print_header();

    if (argc < 2) { usage(); return 1; }

    bufus_cfg_t cfg = parse_args(argc, argv);
    log_init(cfg.log_file[0] ? cfg.log_file : NULL, cfg.verbose);

    /* Administrator check — raw disk I/O requires elevated privilege */
    if (!bufus_is_elevated()) {
        ui_print_error(
            "BUFUS requires Administrator privileges.\n"
            "  Right-click the terminal and choose \"Run as administrator\".");
        log_close();
        return 1;
    }

    /* Enumerate drives (always, so we can display the table) */
    device_list_t devlist;
    device_enumerate(&devlist);

    /* --list mode: just print and exit */
    if (cfg.drive_index == DRIVE_LIST_SENTINEL) {
        ui_list_devices(&devlist);
        log_close();
        return 0;
    }

    /* Validate drive index */
    if (cfg.drive_index < 0) {
        ui_print_error("No target drive specified. Use -d <index> (see --list).");
        log_close();
        return 1;
    }

    device_info_t dinfo;
    if (device_get_info(cfg.drive_index, &dinfo) != BUFUS_OK) {
        ui_print_error("Drive not found. Use --list to see available drives.");
        log_close();
        return 1;
    }

    if (!dinfo.is_removable && !cfg.force) {
        ui_print_error(
            "Refusing to target a fixed/non-removable disk without --force.\n"
            "  Use --list and verify the correct removable drive index.");
        log_close();
        return 1;
    }

    int exit_code;

    if (cfg.benchmark) {
        /* Benchmark doesn't need a source image */
        exit_code = run_benchmark(&cfg, &dinfo);
    } else {
        if (cfg.source[0] == '\0') {
            ui_print_error("No source image specified. Use -s <path.iso>");
            log_close();
            return 1;
        }
        exit_code = run_write(&cfg, &dinfo);
    }

    log_close();
    return exit_code;
}

