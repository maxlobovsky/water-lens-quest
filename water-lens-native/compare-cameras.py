"""Compare one-shot camera diagnostics; no effect on the installed app."""
from pathlib import Path
import json, re
import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont

root=Path(__file__).resolve().parent
results=[]
for eye in (0,1):
    frames=[cv2.imread(str(root/f'build/camera-{size}-eye{eye}-raw.ppm'),cv2.IMREAD_GRAYSCALE) for size in (1280,1728)]
    if any(frame is None for frame in frames): raise RuntimeError('Missing camera diagnostic frames')
    sift=cv2.SIFT_create()
    features=[sift.detectAndCompute(frame,None) for frame in frames]
    candidates=cv2.BFMatcher().knnMatch(features[0][1],features[1][1],k=2)
    matches=[m for m,n in candidates if m.distance<.7*n.distance]
    if len(matches)<12: raise RuntimeError('Not enough scene features for comparison')
    before=np.float32([features[0][0][m.queryIdx].pt for m in matches])
    after=np.float32([features[1][0][m.trainIdx].pt for m in matches])
    matrix,inliers=cv2.estimateAffinePartial2D(before,after,method=cv2.RANSAC,ransacReprojThreshold=2)
    if matrix is None: raise RuntimeError('Could not register the two camera views')
    scale=float(np.hypot(matrix[0,0],matrix[1,0]))
    predicted=before@matrix[:,:2].T+matrix[:,2]
    error=np.linalg.norm(predicted-after,axis=1)[inliers.ravel()>0]
    # This scale includes any head motion. Use the logged head poses to interpret it.
    record=dict(eye=eye,matches=len(matches),inliers=int(inliers.sum()),normalized_image_scale=scale,
                median_error_pixels=float(np.median(error)),translation_pixels=matrix[:,2].tolist())
    results.append(record)
(root/'build/camera-comparison.json').write_text(json.dumps(results,indent=2))
print(json.dumps(results,indent=2))

out=Image.new('RGB',(1100,1260),'#102c30');draw=ImageDraw.Draw(out)
def text(x,y,t,size=22,color='#e6f3e9'):
    draw.text((x,y),t,font=ImageFont.truetype('C:/Windows/Fonts/segoeui.ttf',size),fill=color)
text(30,20,'Camera comparison at 100% view — no zoom',32)
text(30,68,'Actual Quest captures. Both source images displayed at the same size.',18,'#a9c5bf')
for col,size in enumerate((1280,1728)):
    x=30+col*540
    text(x,110,f'{size} × {size} camera mode',25)
    raw=Image.open(root/f'build/camera-{size}-eye0-raw.ppm')
    out.paste(raw,(x,150))
    text(x,677,'Left-eye projection / unavailable pixels shaded',18,'#a9c5bf')
    rendered=Image.open(root/f'build/camera-{size}-eye0-render.ppm').convert('RGBA')
    alpha=Image.open(root/f'build/camera-{size}-eye0-alpha.pgm')
    rendered.putalpha(alpha)
    background=Image.new('RGBA',rendered.size,'#53656a');background.alpha_composite(rendered)
    out.paste(background.convert('RGB'),(x,716))
out.save(root/'camera-comparison.jpg',quality=95)
print(root/'camera-comparison.jpg')
