#!/bin/bash
# Test various DRM refresh parameter combinations to find what works

TEST_DIR="/Users/jameszhu/AI_Projects/remarkableweread/refresh-test-$(date +%Y%m%d-%H%M%S)"
mkdir -p "$TEST_DIR"

echo "=== Testing DRM Refresh Parameters ==="
echo "Display mode: 365×1700"
echo ""

# Create a simple C program that tests different parameter combinations
# Create it inside the Docker container
docker exec qt6-arm-builder bash -c 'cat > /tmp/test_refresh.c << '\''EOF'\''
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <errno.h>

#define DRM_EINK_CONFIG_REFRESH  0xc02064b2
#define DRM_EINK_MODE_QUEUE      0xc01c64ae
#define DRM_EINK_TRIGGER_REFRESH 0xc01064b3

struct drm_eink_config_params {
    uint32_t width;
    uint32_t height;
    uint32_t x;
    uint32_t y;
    uint32_t waveform;
    uint32_t flags;
    uint32_t reserved[2];
} __attribute__((packed));

struct drm_eink_mode_params {
    uint32_t reserved1;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t x1;
    uint32_t x2;
    uint32_t slot;
} __attribute__((packed));

struct drm_eink_refresh_params {
    uint32_t marker;
    uint32_t flags;
    uint32_t reserved[2];
} __attribute__((packed));

int test_refresh(const char* name, uint32_t cfg_w, uint32_t cfg_h, uint32_t cfg_x, uint32_t cfg_y,
                 uint32_t mode_y, uint32_t mode_w, uint32_t mode_h, uint32_t mode_x1, uint32_t mode_x2) {
    printf("\n=== Test: %s ===\n", name);
    printf("CONFIG: width=%u height=%u x=%u y=%u\n", cfg_w, cfg_h, cfg_x, cfg_y);
    printf("MODE: y=%u width=%u height=%u x1=%u x2=%u\n", mode_y, mode_w, mode_h, mode_x1, mode_x2);

    int fd = open("/dev/dri/card0", O_RDWR);
    if (fd < 0) {
        printf("ERROR: Cannot open /dev/dri/card0: %s\n", strerror(errno));
        return -1;
    }

    static uint32_t marker = 1;

    // CONFIG
    struct drm_eink_config_params cfg = {
        .width = cfg_w, .height = cfg_h, .x = cfg_x, .y = cfg_y,
        .waveform = 0, .flags = 0, .reserved = {0, 0}
    };

    if (ioctl(fd, DRM_EINK_CONFIG_REFRESH, &cfg) < 0) {
        printf("  CONFIG FAILED: %s (errno=%d)\n", strerror(errno), errno);
        close(fd);
        return -1;
    }
    printf("  CONFIG: OK\n");

    // MODE_QUEUE
    struct drm_eink_mode_params mode = {
        .reserved1 = 0, .y = mode_y, .width = mode_w, .height = mode_h,
        .x1 = mode_x1, .x2 = mode_x2, .slot = marker
    };

    if (ioctl(fd, DRM_EINK_MODE_QUEUE, &mode) < 0) {
        printf("  MODE_QUEUE FAILED: %s (errno=%d)\n", strerror(errno), errno);
        close(fd);
        return -1;
    }
    printf("  MODE_QUEUE: OK\n");

    // TRIGGER
    struct drm_eink_refresh_params refresh = {
        .marker = marker, .flags = 0, .reserved = {0, 0}
    };

    if (ioctl(fd, DRM_EINK_TRIGGER_REFRESH, &refresh) < 0) {
        printf("  TRIGGER FAILED: %s (errno=%d)\n", strerror(errno), errno);
        close(fd);
        return -1;
    }
    printf("  TRIGGER: OK with marker=%u\n", marker);

    marker++;
    if (marker > 16) marker = 1;

    close(fd);
    printf("✓ All ioctls succeeded\n");
    return 0;
}

int main() {
    printf("Testing DRM E-Ink Refresh Parameters\n");
    printf("=====================================\n");

    // Test 1: xochitl's original params
    test_refresh("xochitl original", 1700, 365, 16, 0, 365, 1700, 730, 16, 16);
    sleep(2);

    // Test 2: Adapted for 365×1700 display (portrait full screen)
    test_refresh("Portrait full screen", 365, 1700, 0, 0, 0, 365, 1700, 0, 0);
    sleep(2);

    // Test 3: Maybe width/height are swapped in config?
    test_refresh("Swapped config, original mode", 365, 1700, 0, 0, 365, 1700, 730, 16, 16);
    sleep(2);

    // Test 4: Minimal region in portrait coords
    test_refresh("Small portrait region", 365, 100, 0, 0, 0, 365, 100, 0, 0);
    sleep(2);

    // Test 5: Perhaps the coordinates are in a different orientation?
    test_refresh("Landscape logic", 1700, 365, 0, 0, 0, 1700, 365, 0, 0);
    sleep(2);

    printf("\n=== All tests complete ===\n");
    return 0;
}
EOF

# Compile for ARM
docker exec qt6-arm-builder bash -c "cd /tmp && aarch64-linux-gnu-gcc -o test_refresh test_refresh.c -static"

# Copy to device
docker cp qt6-arm-builder:/tmp/test_refresh /tmp/
sshpass -p 'QpEXvfq2So' scp -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null /tmp/test_refresh root@10.11.99.1:/tmp/

echo ""
echo "Running tests on device..."
sshpass -p 'QpEXvfq2So' ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@10.11.99.1 'chmod +x /tmp/test_refresh && /tmp/test_refresh' 2>&1 | tee "$TEST_DIR/test-output.log"

echo ""
echo "Results saved to: $TEST_DIR/test-output.log"
