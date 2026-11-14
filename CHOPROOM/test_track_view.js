const assert=require('node:assert/strict');
const View=require('./track_view.js');
const Markers=require('./marker_controls.js');

const duration=223.81569160997734;
const nearStart=View.previewWindow(3.0736573,5.25,duration);
assert(nearStart.from>=0&&nearStart.span>2.17);
assert(nearStart.from<3.0736573);
assert(nearStart.from+nearStart.span>5.25);
const width=820;
const left=View.xAtTime(3.0736573,width,nearStart.from,nearStart.span);
const right=View.xAtTime(5.25,width,nearStart.from,nearStart.span);
assert.equal(View.nearestSide(3.0736573,5.25,nearStart.from,nearStart.span,width,left),'start');
assert.equal(View.nearestSide(3.0736573,5.25,nearStart.from,nearStart.span,width,right),'end');
assert.equal(View.nearestSide(3.0736573,5.25,nearStart.from,nearStart.span,width,(left+right)/2),null);
assert(Math.abs(View.timeAtX(left,width,nearStart.from,nearStart.span)-3.0736573)<1e-8);
assert(Math.abs(View.timeAtX(right,width,nearStart.from,nearStart.span)-5.25)<1e-8);

// Drag an endpoint using the preview coordinate and existing marker guard.
const markers=[0,3.0736573,5.25,7.8];
const shiftedEnd=View.timeAtX(right-65,width,nearStart.from,nearStart.span);
const clamped=Markers.clampBoundary(markers,2,shiftedEnd,duration);
assert(clamped>markers[1]+.03&&clamped<markers[3]-.03);
assert(Math.abs(Markers.clampBoundary(markers,2,1,duration)-3.1036573)<1e-8);
assert(Math.abs(Markers.clampBoundary(markers,2,22,duration)-7.77)<1e-8);

// Horizontal trackpad displacement maps to time; clamping at both ends.
const span=15,visibleWidth=1000;
assert.equal(View.panScroll(50,span,duration,200,visibleWidth),53);
assert.equal(View.panScroll(50,span,duration,-200,visibleWidth),47);
assert.equal(View.panScroll(1,span,duration,-10000,visibleWidth),0);
assert.equal(View.panScroll(220,span,duration,10000,visibleWidth),duration-span);
assert.equal(View.panScroll(0,duration,duration,100,visibleWidth),0);
const intro=View.previewWindow(0,.7,duration);
assert.equal(intro.from,0);
assert(intro.span>=1.5);
const final=View.previewWindow(duration-1.4,duration,duration);
assert(final.from+final.span<=duration+1e-8);
console.log(JSON.stringify({passed:true,checks:18,description:'Two-finger panning and mini-view START/END handle geometry with guarded trim bounds'}));
