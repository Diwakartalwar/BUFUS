/**
 * @file image_probe.h
 * @brief Source image preflight probing and bootability heuristics
 */

#ifndef IMAGE_PROBE_H
#define IMAGE_PROBE_H

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>
#include "bufus.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    IMG_KIND_UNKNOWN = 0,
    IMG_KIND_DISK_IMAGE,
    IMG_KIND_HYBRID_ISO,
    IMG_KIND_ISO9660_ONLY
} image_kind_t;

typedef struct {
    bool has_mbr_signature;
    bool has_gpt_header;
    bool has_iso9660_pvd;
    bool has_uefi_boot_path_hint;
    bool has_protective_mbr_hint;
    image_kind_t kind;
} image_probe_t;

bufus_err_t image_probe(HANDLE src, uint64_t src_size, image_probe_t *out);
bool image_probe_dd_safe(const image_probe_t *p);
const char *image_kind_str(image_kind_t kind);

#ifdef __cplusplus
}
#endif

#endif /* IMAGE_PROBE_H */
