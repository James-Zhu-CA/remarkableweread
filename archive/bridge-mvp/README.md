# Bridge MVP - Shared Memory Producer Demo

This folder contains a minimal producer that writes chessboard frames into a shared memory segment `/dev/shm/weread_frame` using the protocol described in `shm_proto.h`. It is intended to be consumed by a system-Qt epaper viewer.

## Files
- `shm_proto.h` — protocol header.
- `shm_producer_demo.cpp` — standalone producer writing ARGB32 frames at ~2 fps.

## Build (on qt6-arm-builder container)
```bash
cd /tmp/rmpp-qtfb-shim  # or the root where you copied this folder
g++ bridge-mvp/shm_producer_demo.cpp -o bridge-mvp/shm_producer_demo
```
If cross-compiling:
```bash
aarch64-linux-gnu-g++ bridge-mvp/shm_producer_demo.cpp -o bridge-mvp/shm_producer_demo
```

## Run on device
```bash
scp bridge-mvp/shm_producer_demo root@<device>:/tmp/
ssh root@<device> '/tmp/shm_producer_demo'
```
It will create `/dev/shm/weread_frame`, fill chessboard frames, and flip buffers every 500ms until Ctrl+C.

## Consumer (to be implemented)
- A system-Qt (epaper) viewer should mmap `/dev/shm/weread_frame`, validate the header, and display the active buffer when `gen_counter` changes.
- Supported formats: currently ARGB32; extend to RGB565 as needed.
