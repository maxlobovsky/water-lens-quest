const fs = require('node:fs');
const vm = require('node:vm');
const assert = require('node:assert/strict');

// Run application logic without a browser or camera. Real shader compilation
// and visible controls are additionally checked in the browser preview.
const elements = new Map();
const node = id => {
  if (!elements.has(id)) elements.set(id, {
    value: '0', textContent: '', setAttribute() {},
    getContext: () => ({}), addEventListener() {}
  });
  return elements.get(id);
};
const context = vm.createContext({
  document: {getElementById: node, createElement: () => node('sample')},
  window: {addEventListener() {}}, navigator: {}, console, Float32Array, assert
});
const source = fs.readFileSync(require.resolve('../dist/app.js'), 'utf8')
  .replace(/^makeSample\(\);$/m, '')
  .replace(/^initialize\(\);$/m, '');
vm.runInContext(source, context);
vm.runInContext(`
  function near(a,b){assert.ok(Math.abs(a-b)<1e-6, a+' != '+b);}
  const identity = new Float32Array([1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1]);
  const head = new Float32Array([0,0,-1,0,0,1,0,0,1,0,0,0,3,2,4,1]);
  for(const offset of [-.032,.032]){
    // Rotated/translated viewer: inverse eye transform must cancel the common
    // head pose and leave only the measured eye baseline in head coordinates.
    const inverseEye = new Float32Array([0,0,1,0,0,1,0,0,-1,0,0,0,4-offset,-2,-3,1]);
    const relative = multiplyMatrices(new Float32Array(16),inverseEye,head);
    for(let i=0;i<16;i++) near(relative[i],i===12?-offset:identity[i]);
    // Asymmetric frusta still recover the same physical lens center at 2 m.
    const asymmetricOffset=offset<0?.1:-.1;
    const ndcX=(-offset + asymmetricOffset * -2)/2;
    near((ndcX+asymmetricOffset)*2+offset,0);
  }
  const original=effectValues();
  setCycling(true);updateCycle(0,0);
  effectValues().forEach((v,i)=>near(v,original[i]));
  updateCycle(12,12000);
  effectValues().forEach((v,i)=>near(v,presets[0].values[i]));
  updateCycle(24,36000);
  effectValues().forEach((v,i)=>near(v,presets[1].values[i]));
  const before=effectValues();setCycling(false);updateCycle(20,56000);
  effectValues().forEach((v,i)=>near(v,before[i]));
  setCycling(true);
  let previous=effectValues();
  for(let i=0;i<24000;i++){
    updateCycle(1/60,i*1000/60);
    const current=effectValues();
    current.forEach((v,j)=>{
      assert.ok(Number.isFinite(v));
      assert.ok(v>=[0,0,.3,0][j] && v<=[1,1.5,2.5,1][j]);
      assert.ok(Math.abs(v-previous[j])<.004,'Cycle must remain smooth at wraparound');
    });
    previous=current;
  }
  syncControls();element('strength').value='77';element('strength').oninput();
  assert.equal(cycling,false);near(strengthValue,.77);
  element('distance').value='325';element('distance').oninput();near(lensDistance,3.25);
  element('angle').value='85';element('angle').oninput();near(lensAngle,85);
`, context);
console.log('PASS: rotated per-eye geometry, asymmetric projection, cycle continuity/bounds/pause, manual controls');
