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
        "  --mode <auto|raw|extract>  ISO handling mode\n"
        "  --scheme <mbr|gpt|auto>  Partition scheme hint\n"
        "  --boot <bios|uefi|both|auto>  Boot target hint\n"
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

static int parse_scheme_arg(const char *s, bufus_partition_scheme_t *out) {
    if (!s || !out) return 0;
    if (strcmp(s, "auto") == 0) { *out = BUFUS_SCHEME_AUTO; return 1; }
    if (strcmp(s, "mbr") == 0)  { *out = BUFUS_SCHEME_MBR;  return 1; }
    if (strcmp(s, "gpt") == 0)  { *out = BUFUS_SCHEME_GPT;  return 1; }
    return 0;
}

static int parse_boot_arg(const char *s, bufus_boot_mode_t *out) {
    if (!s || !out) return 0;
    if (strcmp(s, "auto") == 0) { *out = BUFUS_BOOT_AUTO; return 1; }
    if (strcmp(s, "bios") == 0) { *out = BUFUS_BOOT_BIOS; return 1; }
    if (strcmp(s, "uefi") == 0) { *out = BUFUS_BOOT_UEFI; return 1; }
    if (strcmp(s, "both") == 0) { *out = BUFUS_BOOT_BOTH; return 1; }
    return 0;
}

static int parse_write_mode_arg(const char *s, bufus_write_mode_t *out) {
    if (!s || !out) return 0;
    if (strcmp(s, "auto") == 0)    { *out = BUFUS_WRITE_AUTO;    return 1; }
    if (strcmp(s, "raw") == 0)     { *out = BUFUS_WRITE_RAW;     return 1; }
    if (strcmp(s, "extract") == 0) { *out = BUFUS_WRITE_EXTRACT; return 1; }
    return 0;
}

static bufus_cfg_t parse_args(int argc, char **argv) {
    bufus_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.drive_index = -1;
    cfg.block_size  = BUFUS_DEFAULT_BLOCK;
    cfg.scheme      = BUFUS_SCHEME_AUTO;
    cfg.boot_mode   = BUFUS_BOOT_AUTO;
    cfg.write_mode  = BUFUS_WRITE_AUTO;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            strncpy(cfg.source, argv[++i], BUFUS_MAX_PATH_LEN - 1);
            cfg.source[BUFUS_MAX_PATH_LEN - 1] = '\0';
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
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            if (!parse_write_mode_arg(argv[++i], &cfg.write_mode)) {
                fprintf(stderr, "  Invalid mode for --mode: %s\n\n", argv[i]);
                usage();
                exit(1);
            }
        } else if (strcmp(argv[i], "--scheme") == 0 && i + 1 < argc) {
            if (!parse_scheme_arg(argv[++i], &cfg.scheme)) {
                fprintf(stderr, "  Invalid scheme for --scheme: %s\n\n", argv[i]);
                usage();
                exit(1);
            }
        } else if (strcmp(argv[i], "--boot") == 0 && i + 1 < argc) {
            if (!parse_boot_arg(argv[++i], &cfg.boot_mode)) {
                fprintf(stderr, "  Invalid boot mode for --boot: %s\n\n", argv[i]);
                usage();
                exit(1);
            }
        } else if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            strncpy(cfg.log_file, argv[++i], BUFUS_MAX_PATH_LEN - 1);
            cfg.log_file[BUFUS_MAX_PATH_LEN - 1] = '\0';
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

static const char *scheme_str(bufus_partition_scheme_t scheme) {
    switch (scheme) {
        case BUFUS_SCHEME_MBR:  return "MBR";
        case BUFUS_SCHEME_GPT:  return "GPT";
        default:                return "auto";
    }
}

static const char *boot_str(bufus_boot_mode_t boot_mode) {
    switch (boot_mode) {
        case BUFUS_BOOT_BIOS: return "BIOS";
        case BUFUS_BOOT_UEFI: return "UEFI";
        case BUFUS_BOOT_BOTH: return "BIOS+UEFI";
        default:              return "auto";
    }
}

static const char *write_mode_str(bufus_write_mode_t mode) {
    switch (mode) {
        case BUFUS_WRITE_RAW:     return "raw";
        case BUFUS_WRITE_EXTRACT: return "extract";
        default:                  return "auto";
    }
}

typedef struct {
    bool has_efi_boot_x64;
    bool has_bootsect;
    bool has_bootmgr;
    bool has_install_wim;
    bool has_install_esd;
} iso_caps_t;

static bufus_partition_scheme_t effective_scheme(const bufus_cfg_t *cfg) {
    if (cfg->scheme != BUFUS_SCHEME_AUTO) return cfg->scheme;
    if (cfg->boot_mode == BUFUS_BOOT_UEFI) return BUFUS_SCHEME_GPT;
    return BUFUS_SCHEME_MBR;
}

static bufus_boot_mode_t effective_boot_mode(const bufus_cfg_t *cfg,
                                             bufus_partition_scheme_t scheme,
                                             const iso_caps_t *caps) {
    if (cfg->boot_mode != BUFUS_BOOT_AUTO) return cfg->boot_mode;
    if (scheme == BUFUS_SCHEME_GPT) return BUFUS_BOOT_UEFI;
    if (caps && caps->has_bootsect && caps->has_efi_boot_x64) return BUFUS_BOOT_BOTH;
    if (caps && caps->has_efi_boot_x64) return BUFUS_BOOT_UEFI;
    if (caps && caps->has_bootsect) return BUFUS_BOOT_BIOS;
    return BUFUS_BOOT_AUTO;
}

static const char *fs_str(bufus_boot_mode_t boot_mode) {
    return (boot_mode == BUFUS_BOOT_BIOS) ? "NTFS" : "FAT32";
}

static bool path_exists_a(const char *path) {
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES;
}

static bool file_size_a(const char *path, uint64_t *out_size) {
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER sz;
    sz.QuadPart = 0;
    bool ok = GetFileSizeEx(h, &sz) != 0;
    CloseHandle(h);
    if (!ok) return false;
    if (out_size) *out_size = (uint64_t)sz.QuadPart;
    return true;
}

static void probe_iso_caps(const char *root, iso_caps_t *caps) {
    char path[MAX_PATH];
    memset(caps, 0, sizeof(*caps));

    snprintf(path, sizeof(path), "%sEFI\\BOOT\\BOOTX64.EFI", root);
    caps->has_efi_boot_x64 = path_exists_a(path);

    snprintf(path, sizeof(path), "%sboot\\bootsect.exe", root);
    caps->has_bootsect = path_exists_a(path);

    snprintf(path, sizeof(path), "%sbootmgr", root);
    caps->has_bootmgr = path_exists_a(path);

    snprintf(path, sizeof(path), "%ssources\\install.wim", root);
    caps->has_install_wim = path_exists_a(path);

    snprintf(path, sizeof(path), "%ssources\\install.esd", root);
    caps->has_install_esd = path_exists_a(path);
}

static void quote_ps_arg(const char *src, char *dst, size_t dstsz) {
    size_t j = 0;
    if (!dstsz) return;
    for (size_t i = 0; src && src[i] && j + 1 < dstsz; i++) {
        if (src[i] == '\'') {
            if (j + 2 >= dstsz) break;
            dst[j++] = '\'';
            dst[j++] = '\'';
        } else {
            dst[j++] = src[i];
        }
    }
    dst[j] = '\0';
}

static bool make_temp_file_a(char *out_path, size_t out_sz, const char *prefix) {
    char tmpdir[MAX_PATH];
    if (!GetTempPathA(sizeof(tmpdir), tmpdir)) return false;
    if (!GetTempFileNameA(tmpdir, prefix, 0, out_path)) return false;
    if (out_sz > 0) out_path[out_sz - 1] = '\0';
    return true;
}

static char pick_free_drive_letter(void) {
    DWORD mask = GetLogicalDrives();
    if (mask == 0) return 0;
    for (char c = 'Z'; c >= 'D'; c--) {
        if ((mask & (1u << (c - 'A'))) == 0) return c;
    }
    return 0;
}

static bool ensure_directory_a(const char *path) {
    char tmp[BUFUS_MAX_PATH_LEN];
    size_t len;

    if (!path || !*path) return false;
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    len = strlen(tmp);
    if (len == 0) return false;

    for (size_t i = 3; i < len; i++) {
        if (tmp[i] == '\\' || tmp[i] == '/') {
            char save = tmp[i];
            tmp[i] = '\0';
            CreateDirectoryA(tmp, NULL);
            tmp[i] = save;
        }
    }
    if (!CreateDirectoryA(tmp, NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) return false;
    }
    return true;
}

static void join_path_a(char *dst, size_t dstsz, const char *a, const char *b) {
    size_t alen = strlen(a);
    bool need_sep = (alen > 0 && a[alen - 1] != '\\' && a[alen - 1] != '/');
    snprintf(dst, dstsz, "%s%s%s", a, need_sep ? "\\" : "", b);
}

static bool ends_with_ci_a(const char *s, const char *suffix) {
    size_t sl = strlen(s), su = strlen(suffix);
    if (sl < su) return false;
    s += sl - su;
    for (size_t i = 0; i < su; i++) {
        char a = s[i], b = suffix[i];
        if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
        if (a != b) return false;
    }
    return true;
}

static bufus_err_t run_cmd(const char *cmd) {
    int rc = system(cmd);
    if (rc != 0) {
        LOGE("Command failed (%d): %s", rc, cmd);
        return BUFUS_ERR_WIN32;
    }
    return BUFUS_OK;
}

static bufus_err_t mount_iso_image(const char *iso_path,
                                   char *out_drive,
                                   size_t out_drive_sz) {
    char letter_file[MAX_PATH];
    char iso_quoted[2 * BUFUS_MAX_PATH_LEN];
    char file_quoted[2 * MAX_PATH];
    char cmd[4096];
    FILE *fp = NULL;
    int ch;

    if (!make_temp_file_a(letter_file, sizeof(letter_file), "bfs")) {
        return BUFUS_ERR_NOMEM;
    }
    DeleteFileA(letter_file);

    quote_ps_arg(iso_path, iso_quoted, sizeof(iso_quoted));
    quote_ps_arg(letter_file, file_quoted, sizeof(file_quoted));

    snprintf(cmd, sizeof(cmd),
             "powershell -NoProfile -ExecutionPolicy Bypass -Command "
             "\"Mount-DiskImage -ImagePath '%s' | Out-Null; "
             "for($i=0; $i -lt 40; $i++){ "
             "$d = (Get-DiskImage -ImagePath '%s' | Get-Volume | Select-Object -First 1 -ExpandProperty DriveLetter); "
             "if($d){ Set-Content -Encoding ASCII -NoNewline -Path '%s' -Value $d; break }; "
             "Start-Sleep -Milliseconds 500 }\"",
             iso_quoted, iso_quoted, file_quoted);

    if (run_cmd(cmd) != BUFUS_OK) return BUFUS_ERR_OPEN;

    fp = fopen(letter_file, "rb");
    if (!fp) {
        DeleteFileA(letter_file);
        return BUFUS_ERR_OPEN;
    }
    ch = fgetc(fp);
    fclose(fp);
    DeleteFileA(letter_file);
    if (ch == EOF || ch == '\0') return BUFUS_ERR_OPEN;

    if (out_drive_sz < 3) return BUFUS_ERR_ARGS;
    out_drive[0] = (char)ch;
    out_drive[1] = ':';
    out_drive[2] = '\0';
    return BUFUS_OK;
}

static void dismount_iso_image(const char *iso_path) {
    char iso_quoted[2 * BUFUS_MAX_PATH_LEN];
    char cmd[2048];
    quote_ps_arg(iso_path, iso_quoted, sizeof(iso_quoted));
    snprintf(cmd, sizeof(cmd),
             "powershell -NoProfile -ExecutionPolicy Bypass -Command "
             "\"Dismount-DiskImage -ImagePath '%s' | Out-Null\"",
             iso_quoted);
    (void)system(cmd);
}

static bufus_err_t create_media_layout(int drive_index,
                                       uint64_t src_size,
                                       uint64_t drive_size,
                                       bufus_partition_scheme_t scheme,
                                       bufus_boot_mode_t boot_mode,
                                       char drive_letter) {
    char script_path[MAX_PATH];
    char cmd[4096];
    FILE *fp = NULL;
    uint64_t src_mb = (src_size + (1024ull * 1024ull - 1ull)) / (1024ull * 1024ull);
    uint64_t drive_mb = (drive_size + (1024ull * 1024ull - 1ull)) / (1024ull * 1024ull);
    uint64_t partition_mb = 0;
    const char *fs = fs_str(boot_mode);

    if (scheme == BUFUS_SCHEME_GPT &&
        (boot_mode == BUFUS_BOOT_BIOS || boot_mode == BUFUS_BOOT_BOTH)) {
        return BUFUS_ERR_ARGS;
    }

    if (!make_temp_file_a(script_path, sizeof(script_path), "bdp")) {
        return BUFUS_ERR_NOMEM;
    }

    partition_mb = src_mb + 512ull;
    if (boot_mode != BUFUS_BOOT_BIOS) {
        if (partition_mb > 30720ull) partition_mb = 30720ull;
    }
    if (drive_mb > 16ull && partition_mb > drive_mb - 16ull) {
        partition_mb = drive_mb - 16ull;
    } else if (drive_mb > 0ull && drive_mb <= 16ull) {
        partition_mb = drive_mb;
    }
    if (partition_mb < 1024ull) partition_mb = 1024ull;

    fp = fopen(script_path, "wb");
    if (!fp) {
        DeleteFileA(script_path);
        return BUFUS_ERR_OPEN;
    }

    fprintf(fp, "select disk %d\r\n", drive_index);
    fprintf(fp, "clean\r\n");
    fprintf(fp, "convert %s\r\n", scheme == BUFUS_SCHEME_GPT ? "gpt" : "mbr");
    if (scheme == BUFUS_SCHEME_GPT) {
        fprintf(fp, "create partition efi size=%llu\r\n", (unsigned long long)partition_mb);
    } else {
        fprintf(fp, "create partition primary size=%llu\r\n", (unsigned long long)partition_mb);
    }
    fprintf(fp, "format fs=%s quick label=BUFUS\r\n", fs);
    fprintf(fp, "assign letter=%c\r\n", drive_letter);
    if (scheme == BUFUS_SCHEME_MBR && boot_mode != BUFUS_BOOT_UEFI) {
        fprintf(fp, "active\r\n");
    }
    fclose(fp);

    snprintf(cmd, sizeof(cmd), "diskpart /s \"%s\" >NUL 2>&1", script_path);
    if (run_cmd(cmd) != BUFUS_OK) {
        DeleteFileA(script_path);
        return BUFUS_ERR_WIN32;
    }

    DeleteFileA(script_path);
    return BUFUS_OK;
}

static bufus_err_t copy_tree_recursive(const char *src_root,
                                      const char *dst_root,
                                      bool skip_install_wim) {
    char pattern[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;

    snprintf(pattern, sizeof(pattern), "%s\\*", src_root);
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        return (err == ERROR_FILE_NOT_FOUND) ? BUFUS_OK : BUFUS_ERR_OPEN;
    }

    do {
        char src_path[MAX_PATH];
        char dst_path[MAX_PATH];

        if (strcmp(fd.cFileName, ".") == 0 || strcmp(fd.cFileName, "..") == 0) {
            continue;
        }

        join_path_a(src_path, sizeof(src_path), src_root, fd.cFileName);
        join_path_a(dst_path, sizeof(dst_path), dst_root, fd.cFileName);

        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (!ensure_directory_a(dst_path)) {
                FindClose(h);
                return BUFUS_ERR_OPEN;
            }
            if (copy_tree_recursive(src_path, dst_path, skip_install_wim) != BUFUS_OK) {
                FindClose(h);
                return BUFUS_ERR_OPEN;
            }
            continue;
        }

        if (skip_install_wim &&
            ends_with_ci_a(src_path, "\\sources\\install.wim")) {
            continue;
        }

        if (!CopyFileA(src_path, dst_path, FALSE)) {
            FindClose(h);
            return BUFUS_ERR_IO_WRITE;
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);
    return BUFUS_OK;
}

static bufus_err_t split_install_wim(const char *src_wim,
                                     const char *dst_sources_dir) {
    char swm_path[MAX_PATH];
    char cmd[4096];
    snprintf(swm_path, sizeof(swm_path), "%s\\install.swm", dst_sources_dir);
    snprintf(cmd, sizeof(cmd),
             "dism /Split-Image /ImageFile:\"%s\" /SWMFile:\"%s\" /FileSize:3800 >NUL 2>&1",
             src_wim, swm_path);
    return run_cmd(cmd);
}

static bufus_err_t run_bootsect(const char *bootsect_path,
                                char target_drive_letter) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "\"%s\" /nt60 %c: /force /mbr >NUL 2>&1",
             bootsect_path, target_drive_letter);
    return run_cmd(cmd);
}

static bufus_err_t run_iso_install(const bufus_cfg_t *cfg,
                                   const device_info_t *dinfo,
                                   uint64_t src_size) {
    char src_drive[4] = {0};
    char target_drive_letter = 0;
    char target_root[4] = {0};
    char src_root[4] = {0};
    char install_wim_path[MAX_PATH];
    char bootsect_path[MAX_PATH];
    iso_caps_t caps;
    bufus_partition_scheme_t scheme = BUFUS_SCHEME_AUTO;
    bufus_boot_mode_t boot_mode = BUFUS_BOOT_AUTO;
    bufus_err_t rc;
    HANDLE dev = INVALID_HANDLE_VALUE;
    bool use_fat32 = true;
    bool split_wim = false;

    rc = mount_iso_image(cfg->source, src_drive, sizeof(src_drive));
    if (rc != BUFUS_OK) {
        ui_print_error("Failed to mount ISO source image.");
        return 1;
    }

    src_root[0] = src_drive[0];
    src_root[1] = ':';
    src_root[2] = '\\';
    src_root[3] = '\0';

    probe_iso_caps(src_root, &caps);
    scheme = effective_scheme(cfg);
    boot_mode = effective_boot_mode(cfg, scheme, &caps);

    if (boot_mode == BUFUS_BOOT_AUTO) {
        dismount_iso_image(cfg->source);
        ui_print_error("This ISO does not expose a supported BIOS or UEFI boot path for extraction mode.");
        return 1;
    }

    if (scheme == BUFUS_SCHEME_GPT &&
        (boot_mode == BUFUS_BOOT_BIOS || boot_mode == BUFUS_BOOT_BOTH)) {
        dismount_iso_image(cfg->source);
        ui_print_error("GPT extraction supports UEFI only. Use --scheme mbr for BIOS or BIOS+UEFI.");
        return 1;
    }

    if ((boot_mode == BUFUS_BOOT_UEFI || boot_mode == BUFUS_BOOT_BOTH) &&
        !caps.has_efi_boot_x64) {
        dismount_iso_image(cfg->source);
        ui_print_error("UEFI extraction requested, but EFI\\BOOT\\BOOTX64.EFI was not found in the ISO.");
        return 1;
    }

    if ((boot_mode == BUFUS_BOOT_BIOS || boot_mode == BUFUS_BOOT_BOTH) &&
        !caps.has_bootsect) {
        dismount_iso_image(cfg->source);
        ui_print_error("BIOS extraction currently needs boot\\bootsect.exe. Use --mode raw for hybrid ISOs or --boot uefi for UEFI-only extraction.");
        return 1;
    }

    use_fat32 = (boot_mode != BUFUS_BOOT_BIOS);
    if (!cfg->force && use_fat32 && src_size > 30720ull * 1024ull * 1024ull) {
        dismount_iso_image(cfg->source);
        ui_print_error("This ISO is too large for the FAT32 extraction path. Use BIOS-only/NTFS or a smaller image.");
        return 1;
    }

    printf("  ISO    : extraction mode  scheme=%s  boot=%s\n\n",
           scheme_str(scheme), boot_str(boot_mode));

    if (cfg->verify) {
        printf("  Note   : --verify is only available for raw image writes.\n\n");
    }

    target_drive_letter = pick_free_drive_letter();
    if (!target_drive_letter) {
        dismount_iso_image(cfg->source);
        ui_print_error("No free drive letter available.");
        return 1;
    }
    target_root[0] = target_drive_letter;
    target_root[1] = ':';
    target_root[2] = '\\';
    target_root[3] = '\0';

    rc = device_open(cfg->drive_index, &dev);
    if (rc != BUFUS_OK) {
        dismount_iso_image(cfg->source);
        ui_print_error(bufus_err_str(rc));
        return 1;
    }

    rc = device_lock(dev, cfg->drive_index);
    if (rc != BUFUS_OK) {
        device_close(dev);
        dismount_iso_image(cfg->source);
        ui_print_error(bufus_err_str(rc));
        return 1;
    }
    device_close(dev);
    dev = INVALID_HANDLE_VALUE;

    if (!cfg->force) {
        char prompt[320];
        snprintf(prompt, sizeof(prompt),
                 "ALL data on PhysicalDrive%d will be repartitioned and erased. Continue?",
                 cfg->drive_index);
        if (!ui_confirm(prompt)) {
            dismount_iso_image(cfg->source);
            printf("\n  Aborted.\n\n");
            return 0;
        }
    }

    printf("  Preparing %s extraction media...\n",
           use_fat32 ? "FAT32" : "NTFS");
    rc = create_media_layout(cfg->drive_index, src_size, dinfo->size_bytes, scheme, boot_mode, target_drive_letter);
    if (rc != BUFUS_OK) {
        dismount_iso_image(cfg->source);
        if (rc == BUFUS_ERR_ARGS) {
            ui_print_error("Requested GPT with BIOS/BOTH boot mode is not supported in extraction mode.");
            return 1;
        }
        ui_print_error("Failed to partition/format target drive.");
        return 1;
    }

    snprintf(install_wim_path, sizeof(install_wim_path), "%s\\sources\\install.wim", src_root);
    split_wim = false;
    if (use_fat32 && path_exists_a(install_wim_path)) {
        uint64_t wim_size = 0;
        if (file_size_a(install_wim_path, &wim_size) &&
            wim_size >= (4ull * 1024ull * 1024ull * 1024ull)) {
            split_wim = true;
        }
    }

    if (copy_tree_recursive(src_root, target_root, split_wim) != BUFUS_OK) {
        dismount_iso_image(cfg->source);
        ui_print_error("Failed while copying ISO contents.");
        return 1;
    }

    if (split_wim) {
        char dst_sources_dir[MAX_PATH];
        join_path_a(dst_sources_dir, sizeof(dst_sources_dir), target_root, "sources");
        if (!ensure_directory_a(dst_sources_dir)) {
            dismount_iso_image(cfg->source);
            ui_print_error("Failed to create destination sources directory.");
            return 1;
        }
        if (split_install_wim(install_wim_path, dst_sources_dir) != BUFUS_OK) {
            dismount_iso_image(cfg->source);
            ui_print_error("Failed to split install.wim for FAT32 media.");
            return 1;
        }
    }

    snprintf(bootsect_path, sizeof(bootsect_path), "%s\\boot\\bootsect.exe", src_root);
    if (boot_mode != BUFUS_BOOT_UEFI) {
        if (!path_exists_a(bootsect_path)) {
            dismount_iso_image(cfg->source);
            ui_print_error("bootsect.exe was not found in the ISO, so BIOS boot setup cannot continue.");
            return 1;
        }
        if (run_bootsect(bootsect_path, target_drive_letter) != BUFUS_OK) {
            dismount_iso_image(cfg->source);
            ui_print_error("bootsect failed to write BIOS boot code.");
            return 1;
        }
    }

    dismount_iso_image(cfg->source);

    rc = device_open(cfg->drive_index, &dev);
    if (rc == BUFUS_OK) {
        disk_refresh_layout(dev);
        device_close(dev);
    }

    ui_print_success();
    return 0;
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
    printf("  Mode   : write=%s  scheme=%s  boot=%s\n\n",
           write_mode_str(cfg->write_mode),
           scheme_str(cfg->scheme),
           boot_str(cfg->boot_mode));

    if (probe.has_iso9660_pvd) {
        bool extract_iso = false;

        if (cfg->write_mode == BUFUS_WRITE_EXTRACT) {
            extract_iso = true;
        } else if (cfg->write_mode == BUFUS_WRITE_AUTO) {
            extract_iso = !image_probe_dd_safe(&probe);
        }

        if (extract_iso) {
            int iso_rc = run_iso_install(cfg, dinfo, src_size);
            CloseHandle(src);
            return iso_rc;
        }
    }

    if (cfg->write_mode == BUFUS_WRITE_EXTRACT && !probe.has_iso9660_pvd) {
        CloseHandle(src);
        ui_print_error("--mode extract requires an ISO9660 source image.");
        return 1;
    }

    if (src_size > dinfo->size_bytes) {
        CloseHandle(src);
        ui_print_error("Source image is larger than the target drive.");
        return 1;
    }

    if (cfg->scheme != BUFUS_SCHEME_AUTO || cfg->boot_mode != BUFUS_BOOT_AUTO) {
        printf("  Note   : scheme/boot controls apply to ISO extraction mode.\n\n");
    }

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

