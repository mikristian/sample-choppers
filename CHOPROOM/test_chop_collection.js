const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const Collection=require('./chop_collection.js');
const Marker=require('./marker_controls.js');
const Export=require('./export.js');

let markers=[0,3.0736573207708213];
let banks=[Collection.emptyBank()];
banks[0][0]=0;
let removed=[];
for(let i=0;i<19;i++){
  const at=4.5+i;
  const added=Marker.insertCut(markers,at,223.81569160997734);
  assert(added.markers,'expected added marker '+i);
  const change=Collection.shiftForInsertion(banks,removed,added.index,markers.length);
  removed=change.removed;markers=added.markers;
}
assert.equal(markers.length-1,20,'Twenty slices remain in time order');
assert.equal(banks.length,2,'Automatic second 16-pad bank');
assert.deepEqual(banks[0],Array.from({length:16},(_,i)=>i));
assert.deepEqual(banks[1].slice(0,4),[16,17,18,19]);
assert.equal(markers[1],3.0736573207708213,'First chop has not moved');

assert(Collection.removeChop(banks,removed,5),'Chop is removed');
assert(removed.includes(5),'Removed chop tracked');
assert(banks.every(bank=>!bank.includes(5)),'Removed chop cleared from all pads');
assert.equal(markers.length-1,20,'Removing chop does not alter source markers');
assert(Collection.restoreChop(banks,removed,5),'Restore works');
assert(!removed.includes(5),'Restored chop no longer removed');
assert(banks.some(bank=>bank.includes(5)),'Restored chop can play on a pad');
const firstBank0=banks[0][0];
banks[0][4]=19;
assert.equal(banks[0][0],firstBank0,'Assignment does not change other pads');
assert.equal(banks[0][4],19,'A chop from Bank B can be assigned to A');

const middle=Marker.insertCut(markers,3.4,223.81569160997734);
const update=Collection.shiftForInsertion(banks,removed,middle.index,markers.length);
markers=middle.markers;removed=update.removed;
assert.equal(banks[0][0],0,'A chop before the insertion stays on its pad');
assert.equal(banks[0][4],20,'An assignment after the insertion follows its original audio');
assert.equal(banks.length,2,'No unnecessary third bank');

const rate=8000,duration=25;
const samples=Float32Array.from({length:rate*duration},(_,i)=>Math.sin(i/17)*0.23);
const audio={sampleRate:rate,numberOfChannels:1,length:samples.length,getChannelData(){return samples;}};
// Use a compact subset of slices so that the export remains light.
const segmentMarkers=[0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18];
const bankA=Array.from({length:16},(_,i)=>i);
const bankB=Collection.emptyBank();bankB[0]=16;bankB[4]=17;bankB[5]=2;
const out=Export.makeCollectionKit(audio,segmentMarkers,[bankA,bankB],[3],{}, {name:'Test Collection',bpm:98});
assert.equal(out.count,17,'All active slices exported, including unassigned ones');
assert.equal(out.banks.length,2);
assert.equal(out.banks[0].pads.length,15,'Removed chop excluded');
assert.equal(out.banks[1].pads.length,3);
assert(out.entries.some(e=>e.name==='chops/018_Test_Collection.wav'));
assert(!out.entries.some(e=>e.name==='chops/004_Test_Collection.wav'));
assert(out.entries.some(e=>e.name==='banks/Bank_B/pad_05_chop_018.wav'));
const dest=path.join(__dirname,'test_chop_collection_kit.zip');
out.blob.arrayBuffer().then(b=>{
  fs.writeFileSync(dest,Buffer.from(b));
  console.log(JSON.stringify({passed:true,banks:banks.length,generatedChops:markers.length-1,exportedChops:out.count,zipBytes:out.blob.size,path:dest}));
}).catch(e=>{console.error(e);process.exitCode=1;});
