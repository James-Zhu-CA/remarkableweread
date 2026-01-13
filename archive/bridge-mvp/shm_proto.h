#ifndef SHM_PROTO_H
#define SHM_PROTO_H

#include <cstdint>

// Shared memory protocol for two-process bridge.
// File: /dev/shm/weread_frame
// Layout:
//   Header (this struct)
//   Buffer0: frame_bytes
//   Buffer1: frame_bytes
// Optionally each buffer may append a uint32_t frame_gen marker.

static constexpr uint32_t SHM_MAGIC = 0x5752464d; // 'WRFM'
static constexpr uint32_t SHM_VERSION = 1;

enum ShmPixelFormat : uint32_t {
    SHM_FMT_ARGB32 = 1,
    SHM_FMT_RGB565 = 2,
};

struct ShmHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t stride;       // bytes per row
    uint32_t format;       // ShmPixelFormat
    uint32_t gen_counter;  // increments on each publish
    uint32_t active_buffer; // 0 or 1
    uint32_t reserved[8];  // room for flags (e.g. full-refresh) or future use
};

#endif // SHM_PROTO_H
