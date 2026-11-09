/* Water Lens. Camera frames are processed locally in WebGL. */
'use strict';
const element = id => document.getElementById(id);
const canvas = element('view');
const video = element('video');
const status = element('status');
const gl = canvas.getContext('webgl', {alpha: true, antialias: false, depth: false, stencil: false, powerPreference: 'high-performance', xrCompatible: true});
let stream = null;
let session = null;
let referenceSpace = null;
let effectEnabled = true;
let waveTime = 0;
let lastTime = 0;
let xrSupported = false;
let cameraBusy = false;
let strengthValue = 0.35;
let speedValue = 0.6;
let scaleValue = 1;
let complexityValue = 0.45;
let lensDistance = 2;
let lensAngle = 70;
let cycling = false;
let cycleTime = 0;
let cycleStart = null;
let controlsTime = 0;
const effectNames = ['strength', 'speed', 'scale', 'complexity'];
const presets = [
  {name:'Gentle swell', values:[0.2,0.3,0.55,0.15]},
  {name:'Fine ripples', values:[0.45,0.65,2.1,0.55]},
  {name:'Wavy glass', values:[0.85,0.15,0.85,0.9]},
  {name:'Moving water', values:[0.65,1.1,1.4,0.7]}
];
const eyeFromHead = new Float32Array(16);
// Column-major multiplication, matching WebXR and WebGL matrix conventions.
function multiplyMatrices(out,a,b) {
  for(let column=0;column<4;column++) for(let row=0;row<4;row++) {
    out[column*4+row]=a[row]*b[column*4]+a[4+row]*b[column*4+1]+a[8+row]*b[column*4+2]+a[12+row]*b[column*4+3];
  }
  return out;
}
function effectValues(){return [strengthValue,speedValue,scaleValue,complexityValue];}
function setEffectValues(values){[strengthValue,speedValue,scaleValue,complexityValue]=values;}
function syncControls(){
  effectValues().forEach((value,i)=>{
    const name=effectNames[i];
    element(name).value=Math.round(value*100);
    element(name+'Value').textContent=(name==='strength'||name==='complexity')?Math.round(value*100)+'%':value.toFixed(2)+'×';
  });
}
function setCycling(enabled){
  cycling=enabled;
  if(enabled){cycleStart=effectValues();cycleTime=0;effectEnabled=true;updateEffectLabel();}
  element('cycle').setAttribute('aria-pressed',String(enabled));
  element('cycle').textContent=enabled?'Pause on this effect':'Slowly explore effects';
  element('cycleStatus').textContent=enabled?'Slowly blending toward Gentle swell…':'Manual settings · adjust any slider';
  syncControls();
}
function updateCycle(delta,time){
  if(!cycling)return;
  cycleTime+=delta;
  // A gentle 12-second entry, then 24 seconds between each preset.
  let from,to,t;
  if(cycleTime<12){from=cycleStart;to=presets[0];t=cycleTime/12;}
  else {
    const phase=(cycleTime-12)/24;
    const index=Math.floor(phase)%presets.length;
    from=presets[index].values;to=presets[(index+1)%presets.length];t=phase-Math.floor(phase);
  }
  const blend=t*t*(3-2*t);
  setEffectValues(from.map((value,i)=>value+(to.values[i]-value)*blend));
  if(time-controlsTime>200){syncControls();element('cycleStatus').textContent='Blending toward '+to.name+' · grip to pause in immersive';controlsTime=time;}
}
let videoCallback = null;
let cameraFrameDirty = false;
let textureSource = null;
let textureWidth = 0;
let textureHeight = 0;
let cameraFrameCount = 0;
let renderFrameCount = 0;
let statsTime = 0;
let previousVideoTime = -1;
function watchCameraFrame() {
  cameraFrameDirty = true;
  cameraFrameCount++;
  videoCallback = video.requestVideoFrameCallback(watchCameraFrame);
}
function updateTexture(live) {
  const source = live ? video : sample;
  const width = live ? video.videoWidth : sample.width;
  const height = live ? video.videoHeight : sample.height;
  if (live && !video.requestVideoFrameCallback && video.currentTime !== previousVideoTime) {
    previousVideoTime = video.currentTime;
    cameraFrameDirty = true;
    cameraFrameCount++;
  }
  const changed = source !== textureSource || width !== textureWidth || height !== textureHeight;
  if (changed) {
    gl.texImage2D(gl.TEXTURE_2D,0,gl.RGBA,gl.RGBA,gl.UNSIGNED_BYTE,source);
    textureSource=source;textureWidth=width;textureHeight=height;
  } else if (live && cameraFrameDirty) {
    gl.texSubImage2D(gl.TEXTURE_2D,0,0,0,gl.RGBA,gl.UNSIGNED_BYTE,video);
  }
  cameraFrameDirty=false;
}
function updateStats(time,live) {
  renderFrameCount++;
  if (!statsTime) statsTime=time;
  const elapsed=(time-statsTime)/1000;
  if(elapsed<1)return;
  const cameraSettings=stream?.getVideoTracks()[0]?.getSettings();
  const data={
    renderFps: Math.round(renderFrameCount/elapsed),
    cameraFps: live ? Math.round(cameraFrameCount/elapsed) : 0,
    resolution: live ? video.videoWidth+'×'+video.videoHeight : 'sample',
    requestedCameraFps:60,
    negotiatedCameraFps:cameraSettings?.frameRate ?? null,
    xrRefreshHz:session?.frameRate ?? null,
    immersive:!!session,
    version:3,
    stereoPresentation:'per-eye projected mono lens',
    effect: {strength:strengthValue,speed:speedValue,scale:scaleValue,complexity:complexityValue,cycling},
    lens: {distanceMeters:lensDistance,horizontalDegrees:lensAngle}
  };
  element('metrics').textContent=data.renderFps+' render fps · '+data.cameraFps+' camera fps · '+data.resolution;
  // Readable from the remote DevTools console. Contains no camera images.
  window.waterLensDiagnostics={...data,status:status.textContent,browser:navigator.userAgent};
  renderFrameCount=0;cameraFrameCount=0;statsTime=time;
}
const sample = document.createElement('canvas');
sample.width = 1280;
sample.height = 960;
const ctx = sample.getContext('2d');
function polygon(points, fill) {
  ctx.beginPath();
  points.forEach(([x,y], i) => i ? ctx.lineTo(x,y) : ctx.moveTo(x,y));
  ctx.closePath(); ctx.fillStyle = fill; ctx.fill();
}
function makeSample() {
  ctx.fillStyle = '#bfcac0'; ctx.fillRect(0,0,1280,960);
  polygon([[0,0],[310,155],[310,690],[0,960]], '#a4b7ae');
  polygon([[1280,0],[1030,155],[1030,690],[1280,960]], '#8da89d');
  polygon([[0,960],[310,690],[1030,690],[1280,960]], '#bda98a');
  ctx.strokeStyle = '#9f8e75'; ctx.lineWidth = 3;
  for (let x=-1200;x<2800;x+=160) {ctx.beginPath();ctx.moveTo(670,530);ctx.lineTo(x,960);ctx.stroke();}
  for (const y of [725,780,855,950]) {ctx.beginPath();ctx.moveTo(0,y);ctx.lineTo(1280,y);ctx.stroke();}
  ctx.fillStyle='#e7ead9'; ctx.fillRect(388,230,315,350);
  const gradient=ctx.createLinearGradient(0,240,0,570); gradient.addColorStop(0,'#9ac5c9');gradient.addColorStop(1,'#e7e9cb');
  ctx.fillStyle=gradient;ctx.fillRect(401,243,289,324);
  polygon([[401,465],[460,421],[530,450],[592,409],[690,449],[690,567],[401,567]],'#91b1a0');
  ctx.fillStyle='#eeeedd';ctx.fillRect(539,240,10,330);ctx.fillRect(400,401,290,10);
  ctx.fillStyle='#597c70';ctx.fillRect(803,292,135,185);ctx.fillStyle='#d6d3b9';ctx.fillRect(815,304,111,161);
  ctx.fillStyle='#b68767';ctx.beginPath();ctx.arc(870,376,32,0,Math.PI*2);ctx.fill();
  polygon([[550,675],[900,675],[1030,760],[530,760]],'#69867b');
  polygon([[530,760],[1030,760],[1030,825],[530,825]],'#4a6b61');
  ctx.fillStyle='#75968a';ctx.fillRect(570,584,325,110);ctx.fillStyle='#85a396';ctx.fillRect(590,600,135,80);ctx.fillRect(739,600,135,80);
  ctx.fillStyle='#3f5f54';ctx.fillRect(550,815,14,65);ctx.fillRect(995,815,14,65);
  ctx.fillStyle='#d6bd93';ctx.beginPath();ctx.ellipse(422,783,118,44,0,0,Math.PI*2);ctx.fill();
  ctx.fillStyle='#806f55';ctx.fillRect(414,810,15,95);
  ctx.fillStyle='#d6ddd0';ctx.fillRect(404,738,27,35);
  ctx.fillStyle='#bd9277';ctx.fillRect(1080,674,75,100);
  ctx.strokeStyle='#486951';ctx.lineWidth=7;ctx.beginPath();ctx.moveTo(1115,692);ctx.lineTo(1115,493);ctx.stroke();
  ctx.fillStyle='#466e53';
  for(let i=0;i<7;i++){ctx.beginPath();ctx.ellipse(1115+(i%2?30:-30),520+i*22,42,14,i%2?-.7:.7,0,Math.PI*2);ctx.fill();}
}
makeSample();

function shader(type, source) {
  const result = gl.createShader(type); gl.shaderSource(result,source);gl.compileShader(result);
  if(!gl.getShaderParameter(result,gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(result));
  return result;
}
let program;
let uniforms;
function setupGraphics() {
  if(!gl) throw new Error('WebGL is unavailable. Try updating your browser.');
  program=gl.createProgram();
  gl.attachShader(program,shader(gl.VERTEX_SHADER,`
    attribute vec2 position;
    varying vec2 uv;
    uniform mat4 projection, eyeFromHead;
    uniform vec2 lensHalfSize;
    uniform float lensDistance;
    uniform mediump float immersive;
    void main(){
      uv=position*.5+.5;
      if(immersive>.5) gl_Position=projection*eyeFromHead*vec4(position*lensHalfSize,-lensDistance,1.);
      else gl_Position=vec4(position,0.,1.);
    }`));
  gl.attachShader(program,shader(gl.FRAGMENT_SHADER,`
#ifdef GL_FRAGMENT_PRECISION_HIGH
    precision highp float;
#else
    precision mediump float;
#endif
    varying vec2 uv;
    uniform sampler2D image;
    uniform float time, strength, aspect, sourceAspect, waveScale, complexity;
    uniform mediump float immersive;
    void main(){
      vec2 p=uv;
      // Preserve camera proportions. The desktop preview uses a crop to fill.
      float ratio=aspect/sourceAspect;
      if(ratio>1.) p.y=(p.y-.5)/ratio+.5;
      else p.x=(p.x-.5)*ratio+.5;
      vec2 q=p*waveScale;
      vec2 flow=vec2(sin(q.y*17.+time*.85)+sin(q.x*11.+q.y*8.-time*.57)*complexity,
                     cos(q.x*15.-time*.74)+sin(q.y*12.-q.x*7.+time*.63)*complexity);
      vec2 warped=clamp(p+flow*strength*.022,vec2(.002),vec2(.998));
      vec3 color=texture2D(image,warped).rgb;
      // Opaque center avoids superimposing delayed camera and passthrough images.
      float edge=1.-smoothstep(.94,1.,length((uv-.5)*2.));
      float alpha=mix(1.,edge,immersive);
      gl_FragColor=vec4(color*alpha,alpha);
    }`));
  gl.linkProgram(program);
  if(!gl.getProgramParameter(program,gl.LINK_STATUS)) throw new Error(gl.getProgramInfoLog(program));
  gl.useProgram(program);
  const buffer=gl.createBuffer();gl.bindBuffer(gl.ARRAY_BUFFER,buffer);
  gl.bufferData(gl.ARRAY_BUFFER,new Float32Array([-1,-1,1,-1,-1,1,-1,1,1,-1,1,1]),gl.STATIC_DRAW);
  const position=gl.getAttribLocation(program,'position');gl.enableVertexAttribArray(position);gl.vertexAttribPointer(position,2,gl.FLOAT,false,0,0);
  uniforms=Object.fromEntries(['image','time','strength','aspect','sourceAspect','immersive','waveScale','complexity','projection','eyeFromHead','lensHalfSize','lensDistance'].map(key=>[key,gl.getUniformLocation(program,key)]));
  gl.bindTexture(gl.TEXTURE_2D,gl.createTexture());
  gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MIN_FILTER,gl.LINEAR);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_MAG_FILTER,gl.LINEAR);
  gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_S,gl.CLAMP_TO_EDGE);gl.texParameteri(gl.TEXTURE_2D,gl.TEXTURE_WRAP_T,gl.CLAMP_TO_EDGE);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL,true);
  gl.uniform1i(uniforms.image,0);
}
function draw(width,height,immersive) {
  gl.uniform1f(uniforms.aspect,width/height);
  gl.uniform1f(uniforms.immersive,immersive?1:0);
  gl.drawArrays(gl.TRIANGLES,0,6);
}
function frame(time,xrFrame) {
  if(session) session.requestAnimationFrame(frame); else window.requestAnimationFrame(frame);
  const delta=lastTime?Math.min((time-lastTime)/1000,.1):0;lastTime=time;
  updateCycle(delta,time);
  waveTime+=delta*speedValue;
  const live=stream && video.readyState>=2;
  updateTexture(live);
  updateStats(time,live);
  gl.uniform1f(uniforms.sourceAspect,live?video.videoWidth/video.videoHeight:sample.width/sample.height);
  gl.uniform1f(uniforms.time,waveTime);
  gl.uniform1f(uniforms.strength,effectEnabled?strengthValue:0);
  gl.uniform1f(uniforms.waveScale,scaleValue);
  gl.uniform1f(uniforms.complexity,complexityValue);
  if(xrFrame && session) {
    const pose=xrFrame.getViewerPose(referenceSpace);if(!pose)return;
    const layer=session.renderState.baseLayer;gl.bindFramebuffer(gl.FRAMEBUFFER,layer.framebuffer);
    gl.clearColor(0,0,0,0);gl.clear(gl.COLOR_BUFFER_BIT);
    const sourceAspect=textureWidth/textureHeight;
    const halfWidth=lensDistance*Math.tan(lensAngle*Math.PI/360);
    gl.uniform2f(uniforms.lensHalfSize,halfWidth,halfWidth/sourceAspect);
    gl.uniform1f(uniforms.lensDistance,lensDistance);
    // One shared head-relative surface. Both eyes see the same geometry using
    // their own unmodified projection and eye position, including asymmetry.
    for(const view of pose.views){
      const v=layer.getViewport(view);if(!v)continue;
      multiplyMatrices(eyeFromHead,view.transform.inverse.matrix,pose.transform.matrix);
      gl.uniformMatrix4fv(uniforms.projection,false,view.projectionMatrix);
      gl.uniformMatrix4fv(uniforms.eyeFromHead,false,eyeFromHead);
      gl.viewport(v.x,v.y,v.width,v.height);
      draw(textureWidth,textureHeight,true);
    }
  } else {
    gl.bindFramebuffer(gl.FRAMEBUFFER,null);
    const width=Math.round(canvas.clientWidth*Math.min(devicePixelRatio,2));
    const height=Math.round(canvas.clientHeight*Math.min(devicePixelRatio,2));
    if(canvas.width!==width||canvas.height!==height){canvas.width=width;canvas.height=height;}
    gl.viewport(0,0,canvas.width,canvas.height);draw(canvas.width,canvas.height,false);
  }
}
function updateEffectLabel(){element('toggle').textContent=effectEnabled?'Compare with clear view':'Restore water effect';element('effectState').textContent=effectEnabled?'RIPPLES ON':'CLEAR VIEW';}
function toggleEffect(){effectEnabled=!effectEnabled;updateEffectLabel();}
element('toggle').onclick=toggleEffect;
element('cycle').onclick=()=>setCycling(!cycling);
for(const name of effectNames)element(name).oninput=()=>{
  // Read the new position before stopping the cycle (which refreshes controls).
  const values=effectNames.map(key=>Number(element(key).value)/100);
  setEffectValues(values);setCycling(false);
};
element('distance').oninput=()=>{lensDistance=element('distance').value/100;element('distanceValue').textContent=lensDistance.toFixed(1)+' m';};
element('angle').oninput=()=>{lensAngle=Number(element('angle').value);element('angleValue').textContent=lensAngle+'°';};
function releaseCamera(){
  if(videoCallback !== null && video.cancelVideoFrameCallback) video.cancelVideoFrameCallback(videoCallback);
  videoCallback=null;cameraFrameDirty=false;previousVideoTime=-1;
  if(stream)stream.getTracks().forEach(track=>track.stop());
  stream=null;video.srcObject=null;element('badge').textContent='SAMPLE ROOM';element('stop').hidden=true;element('immersive').disabled=true;
}

function withTimeout(promise, message, lateResult = () => {}) {
  let expired = false;
  let timer;
  const deadline = new Promise((resolve, reject) => {
    timer = setTimeout(() => { expired = true; reject(new Error(message)); }, 20000);
  });
  promise.then(value => { if (expired) lateResult(value); }, () => {});
  return Promise.race([promise, deadline]).finally(() => clearTimeout(timer));
}
function showStatus(message) {
  console.info('[Water Lens]', message);
  status.textContent = message;
}
async function startCamera(deviceId) {
  if (cameraBusy) return;
  cameraBusy = true;
  const button = element('camera');
  button.disabled = true;
  button.textContent = 'Requesting camera…';
  element('source').disabled = true;
  releaseCamera();
  showStatus('Request received. Waiting for camera permission… Look for a permission prompt in Quest Browser.');
  try {
    if (!window.isSecureContext) throw new Error('Open Water Lens using its https link in Quest Browser.');
    if (!navigator.mediaDevices?.getUserMedia) throw new Error('This browser does not expose camera access. Open the link directly in an updated Quest Browser.');
    const policy = document.permissionsPolicy || document.featurePolicy;
    if (policy && !policy.allowsFeature('camera')) throw new Error('This page is blocked from using cameras by its hosting or browser permissions policy.');
    const cameraConstraints = deviceId ? { deviceId: { exact: deviceId } } : { facingMode: { ideal: 'environment' } };
    stream = await withTimeout(
      navigator.mediaDevices.getUserMedia({video: {...cameraConstraints, width: {ideal:1280}, height: {ideal:960}, frameRate: {ideal:60}}, audio:false}),
      'Camera request timed out. Check the camera permission for this site and the Camera permission for Quest Browser, then retry.',
      lateStream => lateStream.getTracks().forEach(track => track.stop())
    );
    if(video.requestVideoFrameCallback) videoCallback=video.requestVideoFrameCallback(watchCameraFrame);
    const track = stream.getVideoTracks()[0];
    if (!track) throw new Error('The browser returned no video track.');
    const label = track.label || 'Unnamed camera';
    showStatus('Permission granted. Starting video from ' + label + '…');
    video.srcObject = stream;
    await withTimeout(video.play(), 'Camera opened, but video playback did not start. Close other camera tabs and retry.');
    if (video.readyState < 2) {
      await new Promise((resolve, reject) => {
        const timer = setTimeout(() => { cleanup(); reject(new Error('Camera opened, but no video frames arrived. Check Quest Browser camera access and retry.')); }, 15000);
        function cleanup() { clearTimeout(timer); video.removeEventListener('loadeddata', ready); }
        function ready() { cleanup(); resolve(); }
        video.addEventListener('loadeddata', ready);
      });
    }
    element('badge').textContent = 'LIVE CAMERA';
    element('stop').hidden = false;
    element('immersive').disabled = !xrSupported;
    const isAvatar = /avatar|virtual/i.test(label);
    showStatus(isAvatar
      ? 'The browser supplied an avatar camera. Select an outward camera below if available. Otherwise check Quest Browser’s Camera permission.'
      : 'Camera running — ' + label + '. Adjust the ripples, then enter the immersive lens.');
    track.onended = () => { releaseCamera(); showStatus('Camera disconnected. Start it again to reconnect.'); };
    // Device discovery must not prevent a working camera from displaying.
    navigator.mediaDevices.enumerateDevices().then(devices => {
      if (!stream || stream.getVideoTracks()[0] !== track) return;
      const cameras = devices.filter(device => device.kind === 'videoinput');
      const select = element('source');
      select.replaceChildren();
      cameras.forEach((camera,index) => select.add(new Option(camera.label || 'Camera ' + (index+1), camera.deviceId)));
      select.value = track.getSettings().deviceId || '';
      select.disabled = !cameras.length;
    }).catch(() => {});
  } catch (error) {
    releaseCamera();
    const advice = {
      NotAllowedError: 'Camera access was denied or blocked. Allow Camera for this site in Quest Browser, and check the headset’s app permissions for Browser.',
      NotFoundError: 'Quest Browser exposed no camera. Check its Camera permission and update the browser.',
      OverconstrainedError: 'The selected camera is unavailable. Press Start again to discover cameras.',
      NotReadableError: 'The camera could not be opened. Close other camera apps or tabs, then retry.'
    };
    showStatus((advice[error.name] || error.message) + ' [' + error.name + ']');
  } finally {
    cameraBusy = false;
    button.disabled = false;
    button.textContent = 'Start camera / retry';
  }
}
element('camera').onclick=()=>startCamera();
element('source').onchange=()=>startCamera(element('source').value);
element('stop').onclick=async()=>{if(session)await session.end();releaseCamera();status.textContent='Camera stopped. Sample room preview.';};
element('immersive').onclick=async()=>{
  let nextSession;
  try{
    nextSession=await navigator.xr.requestSession('immersive-ar',{requiredFeatures:['local']});
    await gl.makeXRCompatible();
    nextSession.updateRenderState({baseLayer:new XRWebGLLayer(nextSession,gl,{alpha:true})});
    referenceSpace=await nextSession.requestReferenceSpace('local');
    session=nextSession;
    if(session.supportedFrameRates?.length && session.updateTargetFrameRate) {
      const refreshRate=Math.max(...session.supportedFrameRates);
      session.updateTargetFrameRate(refreshRate).catch(error => console.info('[Water Lens] Refresh rate unchanged',error.message));
    }
    session.addEventListener('select',toggleEffect);
    session.addEventListener('squeeze',()=>setCycling(!cycling));
    session.addEventListener('end',()=>{session=null;referenceSpace=null;lastTime=0;gl.bindFramebuffer(gl.FRAMEBUFFER,null);window.requestAnimationFrame(frame);});
    // The pending desktop frame switches the animation loop to the XR session.
    status.textContent='Immersive lens active. Trigger: clear/water. Grip: start/pause the slow effect cycle. Use the system menu to exit.';
  }catch(error){if(nextSession)await nextSession.end();status.textContent=`Immersive mode could not start. ${error.message}`;}
};
async function initialize(){
  syncControls();
  showStatus('Water Lens v3 ready. Per-eye lens alignment + effect explorer. Press Start headset camera.');
  try{setupGraphics();window.requestAnimationFrame(frame);}catch(error){status.textContent=error.message;element('camera').disabled=true;return;}
  try{xrSupported=!!navigator.xr && await navigator.xr.isSessionSupported('immersive-ar');}catch{xrSupported=false;}
  element('immersive').disabled = !stream || !xrSupported;
  if(!/Quest|OculusBrowser/i.test(navigator.userAgent))element('camera').innerHTML='Try a webcam <span>↗</span>';
}
window.addEventListener('pagehide',releaseCamera);
initialize();
