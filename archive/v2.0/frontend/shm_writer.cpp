#include "shm_writer.h"

#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace {
constexpr uint32_t SHM_MAGIC = 0x5752464d; // 'WRFM'
constexpr uint32_t SHM_VERSION = 1;
constexpr uint32_t SHM_FMT_ARGB32 = 1;
}

ShmFrameWriter::ShmFrameWriter() = default;
ShmFrameWriter::~ShmFrameWriter() { cleanup(); }

void ShmFrameWriter::cleanup() {
    if (m_hdr) {
        munmap(m_hdr, m_size);
        m_hdr = nullptr;
    }
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    m_ready = false;
}

bool ShmFrameWriter::init(const QString &path) {
    cleanup();
    m_path = path;
    m_stride = m_width * 4;
    size_t frameBytes = static_cast<size_t>(m_stride) * m_height;
    m_size = sizeof(ShmHeader) + 2 * frameBytes;

    m_fd = ::open(path.toUtf8().constData(), O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (m_fd < 0) {
        qWarning() << "[SHM] open failed" << strerror(errno);
        return false;
    }
    if (ftruncate(m_fd, m_size) != 0) {
        qWarning() << "[SHM] ftruncate failed" << strerror(errno);
        cleanup();
        return false;
    }
    void *base = mmap(nullptr, m_size, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);
    if (base == MAP_FAILED) {
        qWarning() << "[SHM] mmap failed" << strerror(errno);
        cleanup();
        return false;
    }

    m_hdr = reinterpret_cast<ShmHeader *>(base);
    m_buf0 = reinterpret_cast<uint8_t *>(m_hdr) + sizeof(ShmHeader);
    m_buf1 = m_buf0 + frameBytes;

    memset(m_hdr, 0, sizeof(ShmHeader));
    m_hdr->magic = SHM_MAGIC;
    m_hdr->version = SHM_VERSION;
    m_hdr->width = m_width;
    m_hdr->height = m_height;
    m_hdr->stride = m_stride;
    m_hdr->format = SHM_FMT_ARGB32;
    m_hdr->gen_counter = 0;
    m_hdr->active_buffer = 0;

    m_ready = true;
    qInfo() << "[SHM] initialized at" << m_path << "size bytes" << m_size;
    return true;
}

void ShmFrameWriter::publish(const QImage &srcImg) {
    if (!m_ready) return;
    QImage img = srcImg;
    if (img.format() != QImage::Format_ARGB32) {
        img = img.convertToFormat(QImage::Format_ARGB32);
    }
    // Resize/letterbox if needed
    if (img.width() != static_cast<int>(m_width) || img.height() != static_cast<int>(m_height)) {
        img = img.scaled(static_cast<int>(m_width), static_cast<int>(m_height), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }

    uint8_t *dst = (m_hdr->active_buffer == 0) ? m_buf1 : m_buf0; // write to inactive buffer
    for (uint32_t y = 0; y < m_height; ++y) {
        memcpy(dst + y * m_stride, img.constScanLine(y), static_cast<size_t>(m_stride));
    }
    // publish
    m_gen++;
    m_hdr->active_buffer = (m_hdr->active_buffer == 0) ? 1 : 0;
    m_hdr->gen_counter = m_gen;
}
