#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string.h>
#include "image_probe.h"
#include "logger.h"

static bool seek_abs(HANDLE h, uint64_t off) {
    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)off;
    return SetFilePointerEx(h, li, NULL, FILE_BEGIN) != 0;
}

static bool read_exact(HANDLE h, void *buf, DWORD n) {
    DWORD got = 0;
    return ReadFile(h, buf, n, &got, NULL) && got == n;
}

static bool contains_pattern_ci(const unsigned char *buf, DWORD n, const char *pat) {
    size_t plen = strlen(pat);
    if (plen == 0 || n < plen) return false;
    for (DWORD i = 0; i <= n - plen; i++) {
        size_t j = 0;
        for (; j < plen; j++) {
            unsigned char a = buf[i + (DWORD)j];
            unsigned char b = (unsigned char)pat[j];
            if (a >= 'a' && a <= 'z') a = (unsigned char)(a - 'a' + 'A');
            if (b >= 'a' && b <= 'z') b = (unsigned char)(b - 'a' + 'A');
            if (a != b) break;
        }
        if (j == plen) return true;
    }
    return false;
}

bufus_err_t image_probe(HANDLE src, uint64_t src_size, image_probe_t *out) {
    unsigned char sec[2048];
    memset(out, 0, sizeof(*out));
    out->kind = IMG_KIND_UNKNOWN;

    if (!seek_abs(src, 0)) return BUFUS_ERR_IO_READ;
    if (!read_exact(src, sec, 512)) return BUFUS_ERR_IO_READ;
    out->has_mbr_signature = (sec[510] == 0x55 && sec[511] == 0xAA);
    out->has_protective_mbr_hint = out->has_mbr_signature && (sec[450] == 0xEE);

    if (src_size >= 1024) {
        if (!seek_abs(src, 512)) return BUFUS_ERR_IO_READ;
        if (!read_exact(src, sec, 512)) return BUFUS_ERR_IO_READ;
        out->has_gpt_header = (memcmp(sec, "EFI PART", 8) == 0);
    }

    if (src_size >= (uint64_t)16 * 2048 + 6) {
        if (!seek_abs(src, (uint64_t)16 * 2048)) return BUFUS_ERR_IO_READ;
        if (!read_exact(src, sec, 2048)) return BUFUS_ERR_IO_READ;
        out->has_iso9660_pvd = (memcmp(sec + 1, "CD001", 5) == 0);
    }

    {
        const uint64_t max_scan = 128ull * 1024 * 1024;
        const DWORD chunk = 1024 * 1024;
        unsigned char *buf = (unsigned char *)VirtualAlloc(NULL, chunk, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!buf) return BUFUS_ERR_NOMEM;

        uint64_t total = (src_size < max_scan) ? src_size : max_scan;
        uint64_t off = 0;
        while (off < total && !out->has_uefi_boot_path_hint) {
            DWORD want = (DWORD)((total - off) < chunk ? (total - off) : chunk);
            DWORD got = 0;
            if (!seek_abs(src, off) || !ReadFile(src, buf, want, &got, NULL) || got == 0) {
                VirtualFree(buf, 0, MEM_RELEASE);
                return BUFUS_ERR_IO_READ;
            }
            if (contains_pattern_ci(buf, got, "EFI/BOOT/BOOTX64.EFI") ||
                contains_pattern_ci(buf, got, "\\EFI\\BOOT\\BOOTX64.EFI")) {
                out->has_uefi_boot_path_hint = true;
            }
            off += got;
        }

        VirtualFree(buf, 0, MEM_RELEASE);
    }

    if (out->has_gpt_header || out->has_mbr_signature) {
        out->kind = out->has_iso9660_pvd ? IMG_KIND_HYBRID_ISO : IMG_KIND_DISK_IMAGE;
    } else if (out->has_iso9660_pvd) {
        out->kind = IMG_KIND_ISO9660_ONLY;
    } else {
        out->kind = IMG_KIND_UNKNOWN;
    }

    if (!seek_abs(src, 0)) return BUFUS_ERR_IO_READ;
    return BUFUS_OK;
}

bool image_probe_dd_safe(const image_probe_t *p) {
    return p->kind == IMG_KIND_DISK_IMAGE || p->kind == IMG_KIND_HYBRID_ISO;
}

const char *image_kind_str(image_kind_t kind) {
    switch (kind) {
        case IMG_KIND_DISK_IMAGE: return "raw disk image";
        case IMG_KIND_HYBRID_ISO: return "hybrid ISO (raw-writable)";
        case IMG_KIND_ISO9660_ONLY: return "ISO9660 optical image only";
        default: return "unknown image type";
    }
}
