#include "shm_proto.h"

#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace {

constexpr const char *kShmPath = "/dev/shm/weread_frame";
constexpr uint32_t kWidth = 1696;
constexpr uint32_t kHeight = 954;
constexpr ShmPixelFormat kFormat = SHM_FMT_ARGB32; // keep simple for demo
constexpr uint32_t kBpp = 4;
constexpr uint32_t kStride = kWidth * kBpp;
constexpr size_t kFrameBytes = static_cast<size_t>(kStride) * kHeight;
constexpr size_t kHeaderBytes = sizeof(ShmHeader);
constexpr size_t kTotalBytes = kHeaderBytes + 2 * kFrameBytes;

volatile std::sig_atomic_t g_stop = 0;

void handle_signal(int) { g_stop = 1; }

void fill_chessboard(uint8_t *dst) {
    for (uint32_t y = 0; y < kHeight; ++y) {
        bool stripe = ((y / 32) & 1) != 0;
        uint32_t color = stripe ? 0xFFFFFFFFu : 0xFF000000u; // white/black
        uint32_t *row = reinterpret_cast<uint32_t *>(dst + y * kStride);
        for (uint32_t x = 0; x < kWidth; ++x) {
            row[x] = color;
        }
    }
}

} // namespace

int main() {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);

    int fd = ::open(kShmPath, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd < 0) {
        std::perror("open shm");
        return 1;
    }
    if (ftruncate(fd, kTotalBytes) != 0) {
        std::perror("ftruncate");
        return 1;
    }

    void *base = mmap(nullptr, kTotalBytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) {
        std::perror("mmap");
        return 1;
    }

    auto *hdr = reinterpret_cast<ShmHeader *>(base);
    uint8_t *buf0 = reinterpret_cast<uint8_t *>(hdr) + kHeaderBytes;
    uint8_t *buf1 = buf0 + kFrameBytes;

    // init header
    std::memset(hdr, 0, sizeof(*hdr));
    hdr->magic = SHM_MAGIC;
    hdr->version = SHM_VERSION;
    hdr->width = kWidth;
    hdr->height = kHeight;
    hdr->stride = kStride;
    hdr->format = kFormat;
    hdr->gen_counter = 0;
    hdr->active_buffer = 0;

    uint32_t gen = 0;
    bool toggle = false;

    std::cout << "Writing chessboard frames to " << kShmPath
              << " (" << kWidth << "x" << kHeight << "), press Ctrl+C to stop.\n";

    while (!g_stop) {
        uint8_t *target = toggle ? buf1 : buf0;
        fill_chessboard(target);
        // publish
        ++gen;
        hdr->active_buffer = toggle ? 1u : 0u;
        hdr->gen_counter = gen;
        // small delay to avoid hammering
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        toggle = !toggle;
    }

    munmap(base, kTotalBytes);
    close(fd);
    return 0;
}
