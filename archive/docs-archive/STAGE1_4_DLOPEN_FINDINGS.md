# Stage 1.4: dlopen Method - Findings and Limitations

**Date**: 2025-11-18 03:00
**Method**: Dynamic loading of libqsgepaper.so

---

## Test Results

### Library Analysis ✅ Success

**Successfully identified key symbols**:

```cpp
// Singleton getter
EPFramebuffer* EPFramebuffer::instance()
Symbol: _ZN13EPFramebuffer8instanceEv

// Main refresh functions
void EPFramebuffer::swapBuffers(QRect, EPContentType, EPScreenMode, QFlags<UpdateFlag>)
Symbol: _ZN13EPFramebuffer11swapBuffersE5QRect13EPContentType12EPScreenMode6QFlagsINS_10UpdateFlagEE

void EPFramebuffer::framebufferUpdated(const QRect&)
Symbol: _ZN13EPFramebuffer18framebufferUpdatedERK5QRect

// Paper Pro specific
void EPFramebufferAcep2::sendTModeUpdate()
Symbol: _ZN18EPFramebufferAcep215sendTModeUpdateEv

void EPFramebufferAcep2::scheduleTModeUpdate()
Symbol: _ZN18EPFramebufferAcep219scheduleTModeUpdateEv
```

### Runtime Test ❌ Failed

**Test program**: `eink_instance_test.c`

**Results**:
- ✅ Library loads successfully (`dlopen` works)
- ✅ Symbols found via `dlsym`
- ❌ **Segmentation fault when calling `instance()`** (exit code 139)

**Error analysis**:
```
Exit code: 139 (128 + 11 = SIGSEGV)
```

---

## Root Cause

**EPFramebuffer singleton requires Qt platform initialization**

The `EPFramebuffer::instance()` singleton is initialized by Qt's platform plugin during application startup:

1. Application starts with `QT_QPA_PLATFORM=epaper`
2. Qt loads `libepaper.so` (platform plugin)
3. Platform plugin initializes DRM/KMS
4. Platform plugin creates and registers `EPFramebuffer` singleton
5. Only then can `EPFramebuffer::instance()` be called

**Without Qt initialization**:
- The singleton doesn't exist yet
- Calling `instance()` returns NULL or crashes
- Cannot call any EPFramebuffer methods

---

## Architectural Dependencies

```
EPFramebuffer (singleton)
    ↓ requires
Qt Platform Plugin (libepaper.so)
    ↓ requires
Qt Core + Gui initialization
    ↓ requires
QApplication / QGuiApplication
```

**Key dependency**: EPFramebuffer is not a standalone library - it's tightly coupled with Qt's platform abstraction.

---

## Why This Method Fails

### Problem 1: Initialization Order
- EPFramebuffer needs Qt event loop
- Needs QGuiApplication to be constructed
- Needs platform plugin to be loaded first

### Problem 2: Context Requirements
- EPFramebuffer manages DRM/KMS resources
- These are initialized by the platform plugin
- Cannot be created independently

### Problem 3: ABI Complexity
Even if we could initialize Qt:
- Our Qt 6.8.2 is incompatible with system's Qt 6.0.x
- Loading system plugin causes crashes (as we saw earlier)
- Chicken-and-egg problem

---

## Alternative Interpretation

**Could work in Stage 2** (Qt application context):

If we're already running a Qt application:
1. ✅ Qt is initialized
2. ✅ QGuiApplication exists
3. ✅ Could potentially call EPFramebuffer methods

**But**: Still has ABI mismatch problem
- Our Qt 6.8.2 app can't safely call Qt 6.0.x library functions
- Risk of crashes due to incompatible vtables, memory layouts, etc.

---

## Conclusion

### ❌ Method 1 (dlopen) is NOT viable for Paper Pro

**Reasons**:
1. Requires Qt initialization (can't test in standalone C program)
2. Even in Qt context, ABI mismatch prevents safe usage
3. No way to bypass the system library version issue

### Recommendation: Pivot to Method 2

**Method 2: Standard DRM API Investigation**

Instead of trying to use EPFramebuffer, investigate the underlying DRM/KMS mechanism:

1. **Analyze what EPFramebuffer does internally**
   - Use objdump to disassemble key functions
   - Find the DRM ioctl calls it makes
   - Identify custom DRM properties for E-Ink

2. **Reverse engineer the refresh protocol**
   - What ioctl commands are used?
   - What are the parameters?
   - How is the refresh triggered?

3. **Implement direct DRM calls**
   - Bypass EPFramebuffer entirely
   - Make DRM calls directly from our application
   - No ABI compatibility issues

---

## Next Steps: Method 2 - DRM API Analysis

### Step 1: Disassemble EPFramebufferAcep2::sendTModeUpdate

```bash
objdump -d /tmp/libqsgepaper.so > /tmp/qsgepaper_disasm.txt
# Look for the sendTModeUpdate function
grep -A100 "sendTModeUpdate" /tmp/qsgepaper_disasm.txt
```

**Goal**: Find what system calls it makes

### Step 2: Look for DRM ioctl patterns

In the disassembly, look for:
- Calls to `ioctl` (system call)
- DRM property names (strings)
- DRM atomic commit patterns

### Step 3: Identify E-Ink specific DRM properties

Paper Pro likely uses DRM properties like:
- `EINK_REFRESH`
- `EINK_WAVEFORM`
- `EINK_UPDATE_MODE`

These can be set via standard DRM API:
```c
#include <xf86drm.h>
#include <xf86drmMode.h>

int fd = open("/dev/dri/card0", O_RDWR);
drmModeRes *resources = drmModeGetResources(fd);

// Enumerate properties
for (int i = 0; i < connector->count_props; i++) {
    drmModePropertyPtr prop = drmModeGetProperty(fd, connector->props[i]);
    printf("Property: %s\n", prop->name);
}
```

---

## Estimated Time

- ❌ Method 1 (dlopen): **Complete - Not viable** (2 hours spent)
- 🔄 Method 2 (DRM analysis): **3-4 hours** (recommended next step)
- ⏳ Method 3 (Full reverse engineering): **6-8 hours** (fallback)

---

## Status Summary

**Stage 1.4 Status**: Partial - Method 1 failed, pivoting to Method 2

**Key Learnings**:
1. EPFramebuffer cannot be used without Qt initialization
2. ABI mismatch makes system library usage unsafe
3. Must understand the underlying DRM mechanism
4. Paper Pro uses modern DRM/KMS, not legacy framebuffer

**Next Action**: Disassemble and analyze DRM calls in libqsgepaper.so
