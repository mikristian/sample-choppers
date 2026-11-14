const assert=require('node:assert/strict');
const {clampBoundary,nearestHandle,insertCut}=require('./marker_controls.js');

const timeline=[0,1.5,3.2,5.0];
assert.equal(clampBoundary(timeline,0,0.8,12),0.8,'intro marker can move right');
assert.equal(clampBoundary(timeline,0,-7,12),0,'intro marker never moves before start');
assert.equal(clampBoundary(timeline,0,2.8,12),1.47,'intro respects next cut');
assert.equal(clampBoundary(timeline,1,2.6,12),2.6,'internal markers drag freely');
assert.equal(clampBoundary(timeline,1,0,12),.03,'cuts cannot cross before previous marker');
assert.equal(clampBoundary(timeline,2,8,12),4.97,'cuts cannot cross next marker');
assert.equal(clampBoundary(timeline,3,6.8,12),6.8,'outro marker can move right');
assert.equal(clampBoundary(timeline,3,90,12),12,'outro cannot move past audio end');
assert.equal(clampBoundary(timeline,3,2,12),3.23,'outro respects preceding cut');
assert.equal(clampBoundary(timeline,9,1,12),null);
assert.equal(clampBoundary(timeline,0,NaN,12),null);

const w=1200,span=6;
assert.equal(nearestHandle(timeline,0,span,w,7),0,'intro at time zero has accessible handle');
assert.equal(nearestHandle(timeline,0,span,w,300),1,'internal marker is draggable');
assert.equal(nearestHandle(timeline,0,span,w,639),2,'markers get 14px usable hitbox');
assert.equal(nearestHandle(timeline,0,span,w,400),-1,'clicking empty waveform is seeking');
assert.equal(nearestHandle(timeline,3.2,span,w,6),2,'viewed marker at viewport edge is draggable');
assert.equal(nearestHandle(timeline,6,span,w,100),-1,'offscreen handles not selectable');

// Exact regression: The user moved OUTRO to 3.073657s for their first chop.
// Adding the next marker at 5.25s must extend the source range and preserve
// that first chop rather than reject it as outside the selected phrase.
const fullSong=223.81569160997734;
const first=[0,3.0736573207708213];
const second=insertCut(first,5.25,fullSong);
assert.deepEqual(second.markers,[0,3.0736573207708213,5.25]);
assert.equal(second.index,2,'second cut belongs to pad 2 after first chop');
const third=insertCut(second.markers,7.8,fullSong);
assert.deepEqual(third.markers,[0,3.0736573207708213,5.25,7.8]);
assert.deepEqual(third.markers.slice(0,2),first,'original first chop remains unchanged');
const between=insertCut(third.markers,4.1,fullSong);
assert.deepEqual(between.markers,[0,3.0736573207708213,4.1,5.25,7.8],'cuts can be inserted between earlier cuts');
assert.equal(insertCut(first,3.07,fullSong).error,'duplicate');
assert.equal(insertCut(first,3.09,fullSong).error,'duplicate');
assert.equal(insertCut(first,fullSong+.01,fullSong).error,'outside');
assert.equal(insertCut(first,0.005,fullSong).error,'duplicate');
assert.deepEqual(insertCut([2,3.07],1,fullSong).markers,[1,2,3.07],'cuts before a trimmed intro also work');
let many=[0,3.0736573207708213];
for(let i=0;i<15;i++){
  const result=insertCut(many,5+i,fullSong);
  assert(result.markers,'expected chop '+(i+2)+' to be inserted');
  many=result.markers;
}
assert.equal(many.length-1,16);
assert(insertCut(many,50,fullSong).markers,'17th chop can be created and assigned to another bank');
let extended=many.slice();
while(extended.length-1<512){
  const t=extended[extended.length-1]+0.1;
  const result=insertCut(extended,t,fullSong);
  assert(result.markers,'expected another chop '+(extended.length-1));
  extended=result.markers;
}
assert.equal(insertCut(extended,90,fullSong).error,'full','512 chop capacity enforced');
console.log(JSON.stringify({passed:true,tests:28,description:'Intro/outro geometry, full-song cuts and expandable 16-pad banks'}));
