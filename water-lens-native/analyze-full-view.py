"""Analyze local, stationary-headset captures; never uploads imagery.

Requires shared installations of OpenCV and NumPy. Run after Camera, Normal,
SurfaceSolo, SurfaceSweep, SurfaceNear and SurfaceFar unattended captures.
Motion statistics are image measurements, not a stereo-comfort certification.
"""
from pathlib import Path
import json
import os
import shutil
import subprocess
import cv2
import numpy as np

ROOT = Path(__file__).resolve().parent / "build"


def sample_frames(mode, times):
    cap = cv2.VideoCapture(str(ROOT / f"unattended-{mode}.mp4"))
    if not cap.isOpened():
        raise RuntimeError(f"Missing readable {mode} capture")
    frames = []
    for seconds in times:
        cap.set(cv2.CAP_PROP_POS_MSEC, float(seconds) * 1000)
        ok, frame = cap.read()
        if not ok:
            raise RuntimeError(f"Missing {mode} frame at {seconds}s")
        frames.append(frame)
    cap.release()
    return frames


def eye_gray(frame, eye):
    h, w = frame.shape[:2]
    half = w // 2
    # Exclude the black lens surround, keeping the same region in each capture.
    return cv2.cvtColor(frame[int(h*.10):int(h*.88),
                             eye*half+int(half*.10):eye*half+int(half*.90)], cv2.COLOR_BGR2GRAY)


def correspondences(a, b):
    points = cv2.goodFeaturesToTrack(a, 1000, .02, 12)
    if points is None or len(points) < 30:
        raise RuntimeError("Too few scene features for a useful comparison")
    q, status, error = cv2.calcOpticalFlowPyrLK(a, b, points, None,
                                               winSize=(31, 31), maxLevel=4)
    good = (status.ravel() == 1) & (error.ravel() < 15)
    p, q = points[good].reshape(-1, 2), q[good].reshape(-1, 2)
    if len(p) < 30:
        raise RuntimeError("Too few reliable feature tracks")
    return p, q


def motion(mode):
    frames = sample_frames(mode, np.arange(.3, 7.8, .5))
    eyes = []
    for eye in (0, 1):
        a = eye_gray(frames[0], eye)
        distances = []
        for frame in frames[1:]:
            p, q = correspondences(a, eye_gray(frame, eye))
            distances.extend(np.linalg.norm(p-q, axis=1).tolist())
        eyes.append(dict(median_motion_px=float(np.median(distances)),
                         p95_motion_px=float(np.percentile(distances, 95)),
                         tracks=len(distances)))
    return eyes


def immutable_comparison():
    near = sample_frames("surfacenear", [2])[0]
    far = sample_frames("surfacefar", [2])[0]
    eyes = []
    for eye in (0, 1):
        p, q = correspondences(eye_gray(near, eye), eye_gray(far, eye))
        transform, _ = cv2.estimateAffinePartial2D(p, q)
        if transform is None:
            raise RuntimeError("Cannot register near/far captures")
        d = np.linalg.norm(p-q, axis=1)
        eyes.append(dict(tracks=len(p), median_displacement_px=float(np.median(d)),
                         p95_displacement_px=float(np.percentile(d, 95)),
                         scale=float(np.hypot(transform[0, 0], transform[1, 0]))))
    return eyes


def comparison_video():
    # Sequential reads avoid hundreds of expensive decoder seeks.
    modes = ("camera", "surfacesolo")
    caps = [cv2.VideoCapture(str(ROOT / f"unattended-{mode}.mp4")) for mode in modes]
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise RuntimeError("FFmpeg is required for the H.264 comparison")
    video = subprocess.Popen([ffmpeg, "-v", "error", "-y", "-f", "rawvideo", "-pix_fmt", "bgr24",
                              "-s", "1280x800", "-r", "30", "-i", "pipe:0", "-an",
                              "-c:v", "libx264", "-preset", "fast", "-crf", "20",
                              "-pix_fmt", "yuv420p", "-movflags", "+faststart",
                              str(ROOT / "full-view-investigation.mp4")], stdin=subprocess.PIPE,
                             creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    last = [None, None]
    try:
        for index in range(180):
            canvas = np.full((800, 1280, 3), (25, 19, 15), dtype=np.uint8)
            target = .5 + index / 30
            for row, cap in enumerate(caps):
                while last[row] is None or cap.get(cv2.CAP_PROP_POS_MSEC) / 1000 < target:
                    ok, frame = cap.read()
                    if not ok:
                        raise RuntimeError("Comparison capture ended early")
                    last[row] = frame
                y = row * 400
                canvas[y+40:y+400] = cv2.resize(last[row], (1280, 360), interpolation=cv2.INTER_AREA)
                label = ("Camera water | 100% scale | distortion works inside camera boundary" if row == 0 else
                         "Surface alone | full stereo view | maximum waves, no useful distortion")
                cv2.putText(canvas, label, (18, y+27), cv2.FONT_HERSHEY_SIMPLEX,
                            .65, (235, 240, 245), 1, cv2.LINE_AA)
            video.stdin.write(canvas.tobytes())
            if index == 60:
                cv2.imwrite(str(ROOT / "full-view-investigation.jpg"), canvas)
    finally:
        video.stdin.close()
        code = video.wait()
        for cap in caps:
            cap.release()
    if code:
        raise RuntimeError(f"FFmpeg failed with exit code {code}")


if __name__ == "__main__":
    report = {"scope": "Stationary headset, 1920x1080 stereo captures; raw feature motion includes camera noise and tracking jitter.",
              "motion": {mode: motion(mode) for mode in ("normal", "camera", "surfacesolo", "surfacesweep")},
              "immutable_near_vs_far": immutable_comparison()}
    (ROOT / "full-view-analysis.json").write_text(json.dumps(report, indent=2))
    comparison_video()
    print(json.dumps(report, indent=2))
    print("Local comparison: build/full-view-investigation.mp4 and .jpg")
