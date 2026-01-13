# Two-Process Bridge MVP (WeRead → epaper)

Goal: keep WebEngine in existing Qt6 runtime (offscreen), draw to shared memory, and have a tiny system-Qt epaper front-end show the frame on-device. No QTFB/DRM ioctl reliance; epaper plugin handles refresh.

## Data Path
- Shared memory file: `/dev/shm/weread_frame`.
- Header (struct):
  - magic: `0x5752464d` (`WRFM`)
  - version: `1`
  - width: `1696`
  - height: `954`
  - stride: width * 4 (ARGB32) or *2 (RGB565)
  - format: enum `1=ARGB32`, `2=RGB565`
  - gen_counter: uint32_t (increments on every frame publish)
  - active_buffer: uint32_t (0 or 1 for double-buffer)
- Payload:
  - Two frame buffers back-to-back: `stride * height` bytes each.
  - Optional per-buffer `frame_gen` (uint32_t) at end of each buffer to detect partial writes.

## Producer (WeRead offscreen)
- Platform: `QT_QPA_PLATFORM=offscreen` (no display grab).
- Render: QWebEngineView → grab() or QQuickRenderControl to ARGB32 QImage 1696x954.
- Write flow:
  1) Map `/dev/shm/weread_frame`; create + ftruncate to header + 2 * frame_size.
  2) Pick inactive buffer, memcpy QImage bits.
  3) `msync` (or `std::atomic` fence) then increment `frame_gen`, flip `active_buffer`, increment `gen_counter`.
- Rate: throttle to ~5–10 fps (sleep 100–200ms) to limit power.
- Control: expose CLI/env to request full refresh (`WRFM_FULL_REFRESH=1` flag in header reserved field).

## Consumer (epaper front-end)
- Built with system Qt (epaper plugin). QML app + C++ helper.
- C++ helper:
  - mmap `/dev/shm/weread_frame`, validate magic/version/size.
  - Poll `gen_counter` via QTimer (e.g., 100ms). On change, locate `active_buffer`, wrap to QImage (Format_ARGB32 or RGB16).
  - Upload to texture (QQuickImageProvider or QSGSimpleTextureNode) and call `window()->update()`.
  - If `WRFM_FULL_REFRESH` set, force full-screen refresh (epaper plugin should coalesce).
- QML: simple `Image { source: "image://shared/frame" }` or custom texture provider; no heavy UI.

## IPC/Sync Notes
- Use double-buffer + per-buffer `frame_gen` to avoid tearing: consumer only uses buffer whose `frame_gen` == `gen_counter` after copy.
- If shm missing or magic mismatch: show placeholder and retry.
- Clean shutdown: producer can zero magic to signal consumer to stop rendering stale frames.

## Build/Deploy Sketch
- Producer: add a small `ShmFrameWriter` class in your WeRead offscreen launcher; no display deps.
- Consumer: new project `epaper-bridge/` (Qt6 on device). qmake/CMake linking epaper plugin; install binary to `/home/root/bridge/epaper-viewer`.
- Run order:
  1) Start consumer on device: `QT_QUICK_BACKEND=epaper ./epaper-viewer -platform epaper`.
  2) Start producer (can be remote/off-device) with shm mounted on device (or via tcp forward if needed; MVP assumes same device).

## Risks / Mitigations
- WebEngine grab cost: measure; if heavy, consider rendering a slimmed DOM (reader mode) or lower fps.
- Color/format mismatch: default to ARGB32 end-to-end; add RGB565 support only if profiling shows benefit.
- Power: add idle detection—if page static for N seconds, stop pushing frames.
- Networking: WeRead still needs network; ensure device connectivity.

## Next Coding Steps
1) Implement a standalone producer demo (no WebEngine): fill chessboard into shm at 2 fps.
2) Implement consumer QML app to display shm frame; verify on device epaper shows chessboard.
3) Swap producer demo for real WeRead offscreen rendering.
