Water Lens Stereo — native Quest 3 prototype

STATE (2026-11-09)
Version 0.10-full-view-lab (versionCode 10) is built, signature-verified and installed.
It starts in the working Camera mode at 100% view. The failed Full view test is
now labeled Surface test, with an explicit no-visible-water note in its panel.
Worn-headset result: Surface/Full view has no visible water distortion or
boundary; Camera mode restores the water effect and its camera-coverage boundary.
The full-view effect objective is NOT achieved. API success is not visual success.
The Quest accepts the projected layer and mutable mesh. A worn-headset test on
2026-11-09 captured actual compositor output with both eyes and working hand UI.
After startup (~44 then 99 fps), steady state reached ~120 fps, with mutable mesh
updates typically ~0.4-0.5 ms. This is CPU API-call timing, not motion-to-photon latency.
No Camera2 streams run in Full view; the stereo=0 statistic counts app camera
pairs, not compositor stereo. The unused camera width statistic is not a stream.
The recording shows the app and full passthrough; The wearer assessment confirms that
it does not demonstrate a full-view water effect. A later green-window probe establishes that the projected layer is visible and
mutable vertex updates change its footprint. No useful water refraction was observed.
Camera mode retains the distinct 1728x1728 streams and 100-300% size control.
The compact lower-right Controls tab remains head-relative and pinch-operated.
The panel's bottom button switches Surface test / Camera. Both start unzoomed.
Water OFF returns to the regular system reconstruction in either mode.

FULL VIEW EXPERIMENT
FAILED VISUAL RESULT: no visible water distortion reported. Retained only as an
experimental Surface test, never the default. Do not call it a full-view solution.
Native XR_FB_passthrough PROJECTED layer + XR_FB_triangle_mesh. A shared 64x56
grid (3,705 vertices, 7,168 triangles) spans tangent angles +/-2.2 in both axes,
beyond both measured Quest eye frusta. Only inverse depth is animated; the
head-relative ray directions and instance scale remain constant. The compositor
projects its own stereo camera imagery onto the mesh. No app camera image is
enlarged, copied or read back. Camera2 and the app's depth provider stop in this
mode. A reconstruction layer remains underneath, and the hand UI is on top.
The runtime's vertex-only update path is used and its CPU duration is logged.
Initialization/update failures fall back to Camera mode without crashing.

This intentionally supplies synthetic depth. Unlike the Camera shader it cannot
choose arbitrary texture coordinates, and mismatched geometry can change apparent
size, depth and stereo fusion. Full coverage of the mesh is NOT proof that the
runtime supplies valid/distorted camera imagery across the visible lenses.
At zero strength or Water OFF, the projected layer is omitted altogether.
The test mesh remains between 0.8 and 20 meters deep at all control extremes.
Tests check finite depth, fixed angular coordinates, indices/winding, mode switching,
and that Full view ignores the camera zoom control. preview.jpg uses the updated
native UI mesh; it is not a live capture or evidence of full passthrough coverage.

Worn-headset evidence: full-view-test.mp4 (8 seconds of stereo compositor output),
build/full-view-capture.png (MetaCam screenshot), and build/full-view.log.
MetaCam recording failed because its recording app was in the background; native
Android screenrecord succeeded and stopped automatically after eight seconds.
test-full-view.ps1 now uses this proven recording path; it requires an awake headset.
The screenshot contains the actual Controls tab, confirming app-layer visibility.

SURFACE VISIBILITY DIAGNOSTIC (completed on device)
Run .\test-full-view.ps1 -Device <connected Quest endpoint> -SurfaceProbe.
This enables a single-launch surface-test.txt flag, which the app consumes/removes.
Only the projected layer gets a green color map. Vertex X/Y values scale periodically
to make a central window that changes size. Normal reconstruction remains underneath.
The recorded green window is visible in both eyes and changes size. Left-eye
green area varied from 161,726 to 441,985 pixels (2.73x) in the worn-headset clip.
This confirms layer visibility and mutable-buffer updates, but not useful refraction.
The same green surface was subsequently captured with the headset resting on a desk.
The capture helper restores Camera mode in finally, and preserves the original
full-view-test.mp4. New diagnostic evidence uses surface-probe.mp4 and
build/surface-probe.log. Plain captures use build/headset-check.mp4.
Control/geometry tests pass on the Quest, including bounds of the diagnostic window.
The native Meta sample was inspected: layer purpose, mesh/geometry construction,
transforms and composition ordering are consistent; no definitive cause found yet.
https://github.com/meta-quest/Meta-OpenXR-SDK/blob/main/Samples/XrSamples/XrPassthrough/Src/XrPassthrough.cpp

Other approaches considered: combining left/right Camera2 views cannot recover
areas missing from both images; temporal reprojection retains old imagery and
cannot supply a live view of unseen regions. System screen capture adds capture
latency/feedback and is not an appropriate in-app refraction source. The projected
native layer is the experiment because it bypasses the app camera crop entirely.
Meta deprecated the engine integration for projected passthrough, but documents
continued underlying system support. Native APIs remain the route used here.

References:
https://developers.meta.com/vr/documentation/native/android/mobile-passthrough-customization/
https://developers.meta.com/vr/documentation/unity/unity-customize-passthrough-surface-projected-passthrough/
https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/extensions/fb/fb_triangle_mesh.adoc
https://developers.meta.com/vr/documentation/native/native-media-projection/

ACTUAL CAMERA COMPARISON
Both 1280x1280 and 1728x1728 modes were tested at coverage=1, strength=0, depth=1.
A successful paired capture switched modes in the same running app/tracking space.
The head's reported position moved 0.98 mm between the two captures, 2.26 s apart.
Feature registration between normalized raw images found scale 0.99904 (left,
180 inliers) and 0.99944 (right, 153 inliers), median residual about 0.3 pixels.
This shows essentially the same source field of view, not a meaningful increase.
Normalized calibration is unchanged: left fx/fy=0.677791, right=0.677020.
Rendered coverage: left 42.18% -> 42.17%, right 41.83% -> 41.83% of the rectangular
per-eye projection buffer. These are NOT percentages of the lens-visible region.
The camera-1728 mode does not solve unzoomed full-view coverage on this headset.
The first pair had too much head movement for source registration; it was replaced
by the successful same-session pair. Do not use the first pair for scale conclusions.

After restoring normal water animation at 1728/100%, logs show ~120 render fps and
~57-59 stereo-camera fps with depth enabled. These are frame rates, not measured
motion-to-photon latency. GPU readback only occurs in explicit diagnostic mode.
The diagnostic flag was removed and normal mode relaunched. Temporary wake restored.
Build/control tests passed (including reset to 100%), native shaders and cameras
initialized, and the actual camera-comparison.jpg was generated and inspected.
preview.jpg is a separate regenerated native-UI illustration, not headset footage.
Prior APKs remain preserved locally through water-lens-stereo-v8-camera-default.apk.
User recordings are preserved locally and excluded from the public repository.

APP
Package: com.max.waterlens
Activity: com.max.waterlens.WaterLensActivity
Launcher label: Water Lens Stereo
Artifact: water-lens-stereo.apk (development signed, separate from Quest Browser)
Build: run .\build.ps1 in PowerShell.
Shared toolchain: %LOCALAPPDATA%\Android\water-lens-toolchain
Versions: Android NDK r27c; Android API 35/build tools 35; Temurin JDK 17;
OpenXR Android Loader 1.1.65. No Unity installation is required.

IMPLEMENTATION
Native OpenXR + OpenGL ES 3, two distinct Camera2/NDK streams selected by Meta's
camera-source and left/right-position vendor tags. No duplicated mono fallback.
Requests 1728x1728 where supported, else 1280x1280 or 1280x960, at 60 camera fps.
Requests the highest refresh rate exposed by OpenXR and sustained-high GPU/CPU.
Camera frames remain GPU-side via AHardwareBuffer/EGLImage/external texture import.
Normal mode has no networking permission, recording, uploads, CPU video copies or
video encoding. Explicit diagnostics save a single pair of local camera snapshots
and rendered-eye images, then stop readbacks; see CAMERA DIAGNOSTICS below.
Pairs camera timestamps within 10 ms, skips stale frames, uses GPU fences to retain
buffers until rendering finishes, and returns to system passthrough if video stalls.
Uses reported camera intrinsics/extrinsics and capture-time head poses when the
camera reports a known timestamp clock. Unknown clocks use current-head mapping.
Optional XR_META_environment_depth corrects camera-to-eye translation.
Missing depth uses a 2 m approximation, so near objects may not align precisely.
Camera coverage is smaller than full system passthrough: uncovered areas remain
normal system passthrough. At view size 100%, the original calibration is retained.
The default is 100%; adjustable 100-300% angular magnification expands the
effect's footprint by narrowing sampled rays before stereo/depth reprojection.
This changes apparent scene scale/alignment. It does not capture a wider sensor FOV.
The source stream is now 1728 square, with on-device stereo throughput verified.
The edge fade is reduced from 0.2-1.2% to 0.1-0.5% of the camera texture.
Depth discontinuities, moving objects, and disoccluded regions still need headset testing.

CONTROLS
Enable Quest hand tracking, put controllers down and show an open hand.
Use either hand to point at a visible control. A cursor and highlight show the target.
Pinch thumb and index finger together, then release to click the highlighted button.
Calm / Ripples / Deep water / Choppy: choose a preset and stop auto cycling.
Choppy uses speed 7 and sharpness 90%. Speed now reaches 12 (old maximum 1.5).
Sharpness blends smooth sine waves into pointed cubic triangular waves. It changes
wave shape without extra camera texture samples. The speed slider has a quadratic
response to retain fine control at low speeds.
Water ON/OFF: compare against normal passthrough. Auto cycle: pause/resume cycling.
Use + / - for small changes, or pinch and hold a slider handle to adjust continuously.
Strength, Speed, Ripples, Cross waves, Sharpness and View size have labeled rows.
Effect adjustments pause cycling; changing view size leaves cycling unchanged.
The app starts with the main panel hidden. The small Controls tab is at the lower
right of the view, following head position and orientation. Point at it with a hand,
pinch and release to open. Hide returns to the tab. There is no automatic look-to-open
or nearby tap target. The panel itself stays level and world-fixed when open.
Reset restores initial settings (including 100% view) and pauses cycling.
Uses XR_FB_hand_tracking_aim for the runtime's hand ray and pinch detection.
The old left/right gesture mappings and two-hand spread gesture are no longer active.
If native hand aim is unavailable, an intentional open-hand/pinch/release reopens
the panel, then head-ray + pinch selects controls on the world-fixed main panel.
The working Quest runtime reports aim=1 and uses normal hand targeting.
Ray hit tests and rendered controls share the same rectangles in panel_controls.h.
Tracking loss cancels clicks; one hand owns a control until release; small pinch
jitter is tolerated; a pinch acquired while already closed cannot click.
Existing controller controls remain available:
Trigger or A: compare effect against normal system passthrough.
Grip or X: start/pause the slow cycle, including peaked water (starts enabled).
Left stick horizontal: strength; vertical: speed.
Right stick horizontal: ripple density; vertical: cross-current mix.
Moving a stick pauses the automatic cycle.
Quest system menu exits normally.

NEXT DEVICE TEST
Run .\connect-quest.ps1 to reconnect by name and verify the headset serial.
The current APK is already installed. For later builds, use the returned endpoint
with adb -s <endpoint> install -r water-lens-stereo.apk.
Open Water Lens Stereo in the headset's Unknown Sources library and accept Camera
and spatial-data permissions. The app must have permission to read calibration data.
Read adb -s <device> logcat -s WaterLens OpenXR-Loader AndroidRuntime.
Confirm CAMERA logs identify distinct left/right devices and sensible calibration;
confirm STATS stereo > 0 and depth=1 when available. capturePose=1/1 is only expected
for a known camera clock; this Quest currently reports capturePose=0/0.
Compare with normal passthrough, test distant stationary geometry first, then near
objects and head motion. Measure actual frame rates on-device before performance claims.
Do not claim the native app works on hardware from the synthetic preview alone.

WI-FI SETUP AND VERIFIED RESULTS
Wireless ADB was enabled over an authorized USB connection with adb tcpip 5555,
then installation, identity checks and reconnects succeeded with USB unplugged.
connect-quest.ps1 now discovers the attached Quest instead of hard-coding a serial
or IP. It can discover an advertised _adb._tcp service through adb mdns services.
A DNS-SD service instance is not an ordinary DNS hostname; a stopped ADB service
cannot advertise itself. Reboots may require enabling the listener over USB again.
Developer Mode and ADB over Wi-Fi are separate settings. This project uses the
TCP listener, not Android's unrelated TLS/QR pairing flow. A changing DHCP address
is handled by discovery instead of storing a permanent address.

UNATTENDED USB TESTING (verified on 2026-11-09)
Use test-unattended.ps1, which selects a unique connected Quest and prefers USB.
The app consumes a one-launch bench-test.txt file with mode and effect parameters,
then writes local bench-status.json telemetry every half second. No server or
network permission is added. Camera and SurfaceProbe both passed unattended runs
with valid head/view poses and increasing frame counts throughout stereo capture.
Camera: ~119-120 render fps and ~57 stereo camera pairs/sec in the verified run.
SurfaceProbe: ~120 render fps, with the green mutable surface visible in both eyes.
The fixed-headset test does not certify binocular comfort during wearer movement.

Keeping proximity overridden alone was insufficient: Finding position in room
opened after a few seconds and interrupted the app. Temporarily pausing Guardian
for this stationary test kept the app focused. The harness saves the original
proximity/boundary states, uses a finite proximity duration, and restores them in
finally. A saved state file supports recovery after an interrupted host process.
Both settings were verified restored after the completed runs. No PIN, test user,
account reset, network server, or permanent boundary change is involved.

Camera data and diagnostics remain local under build/. The public repository
excludes personal room/screen recordings. Native Android screenrecord captures the
actual stereo compositor. MetaCam screenshots can omit a projected layer even
when it appears in the stereo recording; do not infer layer visibility from those
flat screenshots alone. Prefer screencap or the recorded stereo frames.

FURTHER FULL-VIEW INVESTIGATION (2026-11-09)
All new captures were made over USB with the headset stationary and unworn.
SurfaceSolo pauses and omits the reconstruction underlay, then animates the
projected mesh at strength 1, speed 7, sharpness .9 and view 1. This still did not
produce useful water distortion. SurfaceSweep alternates 0.20 m and 8 m depth
every two seconds while preserving angular extent. SurfaceNear / SurfaceFar
create immutable meshes at those depths, avoiding mutable-buffer caching.
Neither approach produced meaningful image remapping. The immutable comparison
tracked 270 left-eye and 261 right-eye features: median displacement 0.238/0.251
pixels, fitted scales 0.99954/0.99996, at 1920x1080 stereo recording resolution.
These are image measurements, not a guarantee of all possible runtime behavior.
Across sampled frames, median feature motion was 0.20/0.20 pixels for ordinary
passthrough, 0.21/0.22 for surface-only waves, and 10.82/11.29 for the working
Camera water effect. The negative result is comparable with the no-effect
baseline; the same measurement clearly detects the existing water animation.

The service's camera capabilities also advertise a spatial SBS stream with
3456x1728 output and separate per-eye calibration. Meta's NSDK identifies
camera_source=3 as spatial and spatial_format=0 as side-by-side. A GPU SBS import
and calibrated per-eye sampling experiment is implemented behind bench mode 3
(test-unattended.ps1 -Mode Spatial). Actual app enumeration contains only the
avatar and two regular passthrough cameras. The spatial stream is hidden; the
OS reports horizonos.permission.SPATIAL_CAMERA as internal|role. No privilege
changes were attempted. The SBS rendering path is not runtime-validated.

The harness now verifies app version, requested surface experiment and absence
of reconstruction in solo tests. It saves direct screencap output and per-PID
logs; the spatial failure also completed setting restoration. -CameraReadback
saves actual raw/render/alpha images; this produces a brief readback stall.
Use analyze-full-view.py after the relevant captures to produce local image
motion measurements and full-view-investigation.mp4/.jpg. All imagery stays
under ignored build/. The ordinary app still starts in working Camera mode.

The full-view, live stereo distortion objective remains unachieved. Mirroring
or synthesizing missing edge pixels would not reveal the actual peripheral room;
screen capture would add latency and risk feedback from the app's own output.

https://developers.meta.com/vr/reference/metavr-os-nsdk/v207/enum_virtual_camera_8h_mvr_virtual_camera_source/
https://developers.meta.com/horizon/reference/metavr-os-nsdk/v207/enum_virtual_camera_8h_mvr_virtual_camera_spatial_format/
https://developers.meta.com/vr/reference/metavr-os-nsdk/v207/struct_mvr_virtual_camera_configuration/
https://developers.meta.com/vr/documentation/native/android/mobile-passthrough-customization/

PREVIEW / SHADER CHECK
Serve this directory locally and open preview.html. It loads src/shaders.h, compiles
the real fragment logic in WebGL2 (only sampler type differs), then reads back pixels
for each eye at three positions, with depth off / 1 m / 2 m, and coverage 1 / 1.2 / 1.5.
Additional warp checks cover smooth, intermediate and pointed wave shapes.
These new browser checks have not yet been observed passing. Alpha checks verify
expanded coverage. Native HUD shaders/glyphs also
render. A synthetic wall animates after tests. The live preview is
http://127.0.0.1:8766/preview.html. Static preview.jpg is produced by make-preview.py
from the exported C++ quads and font atlas; it is not footage from the headset.
Run .\test-controls.ps1 to build and run the panel tests on the connected Quest.
Use -Preview to regenerate build/panel-preview.json and build/reopen-preview.json
from the actual C++ mesh, then run python make-preview.py for the static UI view.
The glyph atlas is regenerated by make-ui-font.py using the pinned, OFL-licensed Roboto font.
Android vector/adaptive icon resources are in res/ and built with aapt2.
icon.svg is the preview of those same paths, not a separately designed icon.

DESIGN RESEARCH
Meta recommends direct touch for a few nearby buttons, and hand rays + pinch for
larger panels beyond reach. This utility uses a stable floating panel, large targets,
visible hover/pressed states, a cursor, and one selection gesture for either hand.
Apple's button guidance reinforces visible backgrounds and clear interaction states.
We do not rely on Vision Pro-style eye targeting: the normal Quest input is hand aim.

REFERENCES
https://developers.meta.com/vr/documentation/unreal/ts-mqdh-basic-usage/
https://developer.android.com/tools/adb#wireless
https://android.googlesource.com/platform/packages/modules/adb/+/HEAD/docs/dev/adb_wifi.md
https://developers.meta.com/vr/documentation/native/android/pca-native-overview/
https://developers.meta.com/vr/documentation/native/android/pca-native-documentation/
https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/extensions/meta/meta_environment_depth.adoc
https://github.com/oculus-samples/Unity-PassthroughCameraApiSamples
https://github.com/KhronosGroup/OpenXR-Docs/blob/main/specification/sources/chapters/extensions/ext/ext_hand_tracking.adoc
https://github.com/meta-quest/agentic-tools/blob/main/skills/hz-vr-debug/references/screenshots-video.md
https://developers.meta.com/vr/documentation/unity/meta-xr-operator/quest/

https://developers.meta.com/vr/design/hands-ui-best-practices/
https://developers.meta.com/vr/design/raycasting_bp/
https://developers.meta.com/vr/design/indirect_interactions_bp/
https://developer.apple.com/design/human-interface-guidelines/buttons

RECORDING
Quest system menu > Camera > Record video. To stop, Camera > Recording.
The right-controller shortcut is hold Meta, then hold the right trigger; repeat
to stop. Saved clips are in Gallery / Files > Media and Oculus/Videoshots on device.
https://www.meta.com/en-gb/help/quest/516228089650875/

https://developers.meta.com/vr/documentation/unity/unity-pca-overview/

CAMERA DIAGNOSTICS
compare-cameras.ps1 writes a one-shot camera-test.txt flag under the app's external
files directory. A value of 0 captures 1280 then 1728 within one app session; values
1280 or 1728 select a single mode. Requires a worn, awake and tracking headset.
Diagnostic mode disables water strength and uses view=100% for the comparison.
Each eye exports a 512px raw-camera preview, the actual render and an alpha mask.
The graphics readbacks run once per mode; they are absent in ordinary operation.
The PowerShell helper downloads results, removes the flag, restores normal mode,
and restores physical proximity-sensor control in finally.
compare-cameras.py uses globally installed OpenCV/Pillow/NumPy to register source
images and produce camera-comparison.jpg. Saved evidence is in build/:
  camera-comparison.log / camera-comparison.json: successful same-session A/B.
  camera-performance.log: longer initial performance runs for both resolutions.
  camera-normal.log: restored 1728 mode with the animated water effect enabled.
Raw .ppm and alpha .pgm files are reused by subsequent diagnostics.
