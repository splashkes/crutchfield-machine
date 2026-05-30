# Camera input · Continuity Camera + USB

The External layer pulls one camera into the feedback engine as the source signal (Crutchfield's `f · I_n` term · the incoming raster).

On macOS the camera path is AVFoundation. The device discovery list now includes the modern types so an iPhone over Continuity Camera shows up as a first-class source alongside the built-in FaceTime cam, USB webcams, and OBS Virtual Camera.

## Quick start

```bash
# Enumerate what AVFoundation can see, then exit.
./feedback --list-cameras

# Pick a device by substring of name / model / uniqueID (case-insensitive).
./feedback --camera phone        # iPhone via Continuity Camera
./feedback --camera facetime     # force built-in even when phone is plugged in
./feedback --camera obs          # OBS Virtual Camera
./feedback --camera desk         # iPhone Desk View Camera

# Default (no flag): first device returned by AVFoundation.
./feedback
```

## Continuity Camera prerequisites

iPhone:

- iOS 16 or later · iPhone XR or newer
- Bluetooth ON
- Wi-Fi ON (same network as Mac OR connected by Lightning/USB-C data cable)
- Apple ID matches the Mac's
- Settings → General → AirPlay & Continuity → **Continuity Camera = ON**
- Locked screen or recent home-screen state · iOS gates Continuity on the phone being available rather than actively in use

Mac:

- macOS 13 (Ventura) or later · macOS 14 (Sonoma) recommended for the `AVCaptureDeviceTypeContinuityCamera` device class
- Bluetooth ON
- Wi-Fi ON (even when iPhone is on a USB data cable, the Continuity handshake runs over BT + Wi-Fi)
- Same iCloud account as the iPhone

When all of the above are green, `--list-cameras` should show `SEAN'S PHONE Camera` (or similar) with `type: AVCaptureDeviceTypeContinuityCamera` or `AVCaptureDeviceTypeExternal`. If the phone is plugged in via USB the relationship is more reliable.

## Discovery types added

`camera_avfoundation.mm:140-160` builds the discovery list. On macOS 14+ the list is:

- `AVCaptureDeviceTypeBuiltInWideAngleCamera`
- `AVCaptureDeviceTypeExternal`
- `AVCaptureDeviceTypeContinuityCamera`
- `AVCaptureDeviceTypeDeskViewCamera`

On older macOS, falls back to the deprecated `AVCaptureDeviceTypeExternalUnknown`.

## Device selection

`Camera::open(int w, int h, const char* match)` takes an optional substring filter. The filter is case-insensitive and matches against `localizedName`, `modelID`, and `uniqueID` in that order. If `match` is provided and no device matches, the engine falls back to the first available with a warning logged.

The CLI flag `--camera <substring>` populates this match string. Empty / absent flag = first available.

## What the camera feeds

The output of the chosen device is wired to the External layer (Crutchfield's `f · I_n(bRx)` incoming-raster term). External strength is the `external` parameter, drivable from the cockpit, OSC (`dyn.external.axis`), and the raw param editor.

## Smoke test

```bash
# Enumerate.
./feedback --list-cameras

# Launch with phone, watch the camera log line.
./launch.sh --camera phone
./launch.sh tail | head -20
# expect: [camera] using "SEAN'S PHONE (2) Camera" (iPhone18,2)
# expect: [camera] negotiated 1920x1080 BGRA
```

If the phone shows up in `--list-cameras` but `[camera] timed out waiting for first frame` appears in the log, wake the phone, ensure the lock screen isn't on the camera app, and relaunch. Continuity Camera occasionally needs a wake-and-retry on the iPhone side.

## Implementation pointers

- `camera.h` · interface · `open(w, h, match)` + `listDevices()` static
- `camera_avfoundation.mm` · macOS backend
  - `discoveryTypes()` · the type list (modern vs legacy)
  - `deviceMatches()` · case-insensitive substring match across name / model / uniqueID
  - `chooseDevice(match)` · filtered selection with fallback
  - `Camera::listDevices()` · stdout enumeration for `--list-cameras`
- `camera.cpp` · Windows Media Foundation stub for symmetry · ignores `match`
- `main.cpp` · CLI flag parsing (`--camera`, `--list-cameras`), camera setup at startup

## Related

- [DYNAMICS.md](DYNAMICS.md) · cockpit
- [MP4_RECORDER.md](MP4_RECORDER.md) · record what the camera + engine produces
