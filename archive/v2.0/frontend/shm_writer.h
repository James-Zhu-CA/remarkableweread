#pragma once

#include <QImage>
#include <QString>

struct ShmHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;      // 1=ARGB32, 2=RGB565
    uint32_t gen_counter;
    uint32_t active_buffer; // 0/1
    uint32_t reserved[8];
};

class ShmFrameWriter {
public:
    ShmFrameWriter();
    ~ShmFrameWriter();

    bool init(const QString &path = "/dev/shm/weread_frame");
    void publish(const QImage &img);

private:
    void cleanup();

    QString m_path;
    int m_fd = -1;
    size_t m_size = 0;
    ShmHeader *m_hdr = nullptr;
    uint8_t *m_buf0 = nullptr;
    uint8_t *m_buf1 = nullptr;
    uint32_t m_width = 954;
    uint32_t m_height = 1696;
    uint32_t m_stride = m_width * 4;
    bool m_ready = false;
    uint32_t m_gen = 0;
};
