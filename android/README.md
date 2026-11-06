# Android tablet and phone prototype

The Android app lives in the same repository as the Windows application.
It shares the mesh kernel, brushes, SC1/OBJ codecs, document operations,
camera state and stroke interaction. Windows-specific commands and handles
are isolated in `src/platform/windows/`. Android uses a `GLSurfaceView`
host with all native state confined to its render thread, OpenGL ES 3.0,
and Dear ImGui with a small touch-oriented panel.

## Development tools

On the development machine all tools live in `F:\dev\Android`:

- Temurin JDK 21, Gradle 8.13, Android Gradle Plugin 8.13.2
- SDK platform 36, Build Tools 35.0.0, Platform Tools
- NDK 30.0.16248370, CMake 3.31.6 and its bundled Ninja

`F:\dev\Android\env.ps1` selects the tools and routes Android user data,
Gradle caches and temporary files to F:. `android/build.ps1` loads it
automatically. `local.properties` is local and ignored; `ANDROID_HOME`
also supplies the SDK path. The debug signing key is stored in
`ANDROID_USER_HOME`. No emulator or Android Studio is required.

The app requires Android 9/API 28, ARM64 and OpenGL ES 3.0. Compile SDK 36
does not raise that minimum. The tested device is a Samsung Galaxy Tab S3
SM-T820 with Android 9 and an Adreno 530.

```powershell
# Build only
.\android\build.ps1

# Build, install and start on the one connected USB device
.\android\build.ps1 -Install

# Optional: select a device explicitly
.\android\build.ps1 -Install -Serial <adb-serial>

# Build and run the device smoke test, then start the app
.\android\build.ps1 -SmokeTest
```

The APK is `android/app/build/outputs/apk/debug/app-debug.apk`. This is a
development build, not a release signed for distribution. Keep the debug
keystore to allow installation of updates without uninstalling the app.

## Controls and current scope

- S Pen: sculpt; pressure changes strength of surface brushes during the
  same stroke. Pen contact beside the model orbits the view.
- Pen side button / eraser tool: invert for the stroke.
- One finger on the model: sculpt, like the pen at full pressure.
- One finger beside the model: orbit. A second finger, anywhere, turns that
  into pan and pinch zoom. Once one of the two lifts, the view holds still
  until the other lifts too. The first finger alone decides: a second finger
  during a stroke or on a control does nothing. A model filling the whole view
  leaves no room beside it; Frame in the view bar brings it back.
- Contact recognition remains the same as the confirmed-working physical
  S Pen build. Raw samples enter a FIFO, including history, pressure,
  corners and down/up/cancel boundaries. The render thread processes them for
  up to 8 ms before drawing. Hover keeps only its latest position; contact
  motion is kept exactly up to 16 queued samples, then merges into the newest
  one, so slow brushes on large meshes cannot build an ever-growing lag.
- The view renders on input and while a stroke, mesh cleanup or recovery save
  is pending, then stops for an idle tablet instead of drawing continuously.
- Grab accepts these samples cheaply and applies its latest target once per
  frame. Release, pause and file operations flush the final target even if no
  frame intervened. Other brushes retain their full movement history.
- Grab behaves as on the desktop and in Sculptris: it regrips along the pen
  path, so curved movements pull bent tentacles. Grab, Rotate and Scale ignore
  pressure and follow the pen fully. The experimental anchored Grab
  (`BrushSettings::anchoredGrab`) held the initial region and could only pull
  straight spikes; it is off on Android as well.
- Grab/Rotate/Scale use the fixed view plane through the initial visible hit,
  including mirrored hits. Their radius stays fixed during contact; later
  surface hits cannot change the drag depth or brush size. Surface brushes
  continue to follow the surface as before.
- Hover exit is independent of contact cancellation. Hover motion cannot
  change the pressure of a contacting stroke or terminate it.
- Fingers also operate panel controls. Pen contacts own their stroke;
  concurrent finger contacts do not sculpt or move the camera. Fingers, and so
  a resting hand, are ignored while the pen touches or hovers and for 400 ms
  after; a pen arriving during a finger contact ends that contact. Fingers go
  through the pen's FIFO and hit test (`FingerGesture.java`), because only the
  render thread can pick the mesh.
- The UI follows the desktop app, compacted for the tablet: the shared theme
  and icon widgets (`src/app/widgets.*`) and a tool strip with all eleven
  tools. As in Sculptris, the brush settings lie in a translucent bar over
  the top of the view: tool, Size, Strength, Detail, the material sphere, a
  menu (Open, Save, Export OBJ, New sphere); the second row holds the tool's
  options, the pen switches and symmetry. A tab centered under the bar folds
  that row away and back.
  A view bar offers Undo, Frame and Wireframe; the triangle count and
  messages are dim text in the corner. Pen and fingers share one hit test
  (`uiAt`) for what belongs to the UI.
- Phones get their own layout: below 600 dp on the shorter screen side the
  model fills the whole landscape screen. On the left are the tool button,
  Undo, Frame and Wireframe, on the right the axes, upright Size and Strength
  sliders and Invert. The tool button opens all tools with large icons, the tab
  at the top opens Detail, symmetry, the material, the menu and the tool's
  options. A tap beside an open panel only closes it. The panels open on a tap
  because swiping in from the edge belongs to Android, and the UI keeps clear
  of a camera hole (`nativeInsets`). Pressure has no switch here; it stays on.
- Material... opens a panel of the built-in matcaps, as in Sculptris. Red wax,
  fitted to the Sculptris default, is the start material. With symmetry on, a
  faint light line marks the mirror plane on the mesh.
- Open and Save use Android's document picker for unpainted SC1 models;
  OBJ exports geometry. Save creates a snapshot before opening the picker.
- Recovery saves after settled strokes and on pause, using a temporary
  file followed by rename. Like Sculptris, every launch starts with the
  sphere; the session is restored only when Android recreates an activity it
  killed in the background, for example behind the document picker.
  Recovery is a single session, not a replacement for saving named models.
- Undo snapshots have a 256 MiB budget and at most 20 entries. A mesh
  operation is refused if even its initial snapshot exceeds the budget; a
  stroke still sculpts, without an undo step, and says so in the status line.

Painting, image/stamp import, collapsible panels, subdivision/reduce-all UI,
advanced stylus calibration and large-mesh optimization remain follow-up
work. Painted SC1 files are explicitly rejected, keeping
the current sculpt document intact. Import is limited to 64 MiB.

## Validation

The normal host CTest suite includes a window-independent `interaction`
test for the undo budget, refusal of over-budget edits, and changing pen
pressure within one undoable stroke. It also compares local Grab normals
against full geometry recomputation and verifies frame-batched Grab's exact
final target and pressure, including release before the next frame.
`grab_test` reproduces loss of grip during large pointer jumps, unwanted
recapture of remote geometry, dependence on movement sample count, release
pressure behavior and an incorrect mirrored view plane. These tests failed
before the anchored Grab/view-plane correction and pass with it.

`android/tests/PenEventQueueTest.java` runs on the desktop JVM without Android
input injection. Compile it together with `PenEventQueue.java`, then run
`org.bildhauer.app.PenEventQueueTest`. It checks lossless history, pressure,
corners, contact boundaries and concurrent production/consumption.
`android/tests/FingerGestureTest.java` runs the same way, with
`FingerGesture.java` added. It checks how finger contacts become samples:
pinch center and distance, extra fingers, lifting order and cancellation.

The Android smoke instrumentation injects stylus events with varying
pressure through the actual Android input path. It checks mesh changes,
recovery creation, exact SC1 restoration through the Undo button, SC1
reopening and OBJ export. It saves a screenshot in the app's external files
directory as `smoke-stroke.png`. Run it with a model visible and no dialogs
open; it temporarily sculpts the current model and restores it using Undo.
These are injected events, not readings from the physical digitizer.
A passing test does not establish
actual S Pen detection, latency, pressure feel or palm rejection quality;
those still require hands-on testing.

The ES3 ImGui backend is from the official v1.92.9 release, matching the
existing vendored ImGui version, under its existing MIT license.

## Grab performance measurements

The optional `BILDHAUER_ANDROID_OPTIMIZE_DEBUG` setting compiles native
Debug code with `-O2` while retaining symbols. It defaults to OFF for plain
CMake builds; the Android Gradle project explicitly enables it for the
compiler-only comparison with the confirmed-working unoptimized APK.
The first prototype used the default unoptimized Debug configuration.
On the Tab S3, an isolated benchmark on a 21,324-triangle session model
produced these times for 60 Grab updates (excluding rendering and saving):

| Brush radius / model size | Unoptimized | Optimized |
|---|---:|---:|
| 0.03 | 1,499 ms | 157 ms |
| 0.10 | 3,351 ms | 398 ms |
| 0.25 | 5,536 ms | 725 ms |

The benchmark preserves the same final triangle counts in both builds.
These figures isolate compiler optimization, which the user subsequently
confirmed to retain physical pen input and improve Grab on 2026-10-28.
Further real-pen logs showed 300-600 updates in short strokes and rendering
starved behind `GLSurfaceView.queueEvent()`. The current version therefore
uses a lossless frame-drained raw FIFO and applies Grab once per frame.
Grab also updates geometry around this dab's changed/new vertices instead
of every vertex touched anywhere earlier in the stroke. Transform drags
now derive their target from a frozen view plane without a new surface pick;
the earlier lossy raw-input coalescing remains reverted. The existing
cross-product picking correction from commit fbf108a is retained.

One mesh update can still exceed the event-processing budget with a large
brush/model; it is not preemptible. Remaining per-frame cost must be measured
separately from historical-input backlog. Automated pointer injection remains
suspended; tablet validation uses the physical pen. The cause of the earlier
system-wide pen outage is unconfirmed; it cleared after a complete power-off.

To build the standalone benchmark, configure CMake for Android with
`-DBILDHAUER_BUILD_BENCHMARKS=ON -DANDROID_STL=c++_static`, build target
`grab_bench`, and run `grab_bench model.sc1 60` on the device. It works on a
copy in memory and never writes the input model.
