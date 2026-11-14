const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const Variants=require('./chop_variants.js');
const Collection=require('./chop_collection.js');
const Export=require('./export.js');
// Synthetic 24-chop fixture; no recorded song or private saved session required.
const session={duration:24,markers:Array.from({length:25},(_,i)=>i),
  banks:[Array.from({length:16},(_,i)=>i),Array.from({length:16},(_,i)=>i<8?i+16:null)]};
session.banks[0][5]=13;
session.banks[0][11]=22;
session.banks[0][12]=18;
session.banks[0][13]=19;
session.banks[1][6]=5;
const before=JSON.stringify(session.markers);
const variants=[];
const original=Variants.getRange(13,session.markers,variants); // Chop 14
assert(original&&original.end>original.start);
const full=Variants.copyRange(13,session.markers,variants);
assert(full&&full.id==='v1');
assert(Math.abs(full.end-full.start-(original.end-original.start))<1e-10);
const short=Variants.copyRange(13,session.markers,variants);
assert(short.id==='v2');
Variants.trimCopy(short.id,session.markers,variants,'end',(short.start+short.end)/2,session.duration);
assert(Math.abs(short.end-short.start-(original.end-original.start)/2)<1e-8);
assert(Math.abs(full.end-full.start-(original.end-original.start))<1e-8,'full copy must stay full');
assert.equal(JSON.stringify(session.markers),before,'original waveform boundaries must not change');
const halves=Variants.splitRange(full.id,session.markers,variants);
assert.equal(halves.length,2);
assert.equal(halves[0].end,halves[1].start,'halves meet without gaps');
assert.equal(halves[0].start,original.start);
assert.equal(halves[1].end,original.end);
assert.equal(Variants.getRange(13,session.markers,variants).end,original.end,'original still intact');
const custom=Variants.splitRange('v2',session.markers,variants,short.start+(short.end-short.start)*.2);
assert(custom&&custom[0].end<custom[1].end);
assert.equal(Variants.normalize(variants,session.markers,session.duration).length,6);
assert.equal(Variants.nextId(variants),'v7');
const copy=variants.find(v=>v.id==='v2');
assert.equal(Variants.trimCopy(copy.id,session.markers,variants,'start',-100,session.duration).start,0);
// Reset the second copy to its intended half after boundary clamping test.
copy.start=short.start;
const banks=session.banks.map(x=>x.slice());
const location=Collection.assignFirstAvailable(banks,full.id);
assert(location&&banks[location.bank][location.pad]===full.id);
const second=Collection.assignFirstAvailable(banks,short.id);
assert(second&&banks[second.bank][second.pad]===short.id);
const changed=Collection.movePad(banks,location.bank,location.pad,1,6); // swap full copy with Bank B Pad 7
assert(changed.swapped);
assert.equal(banks[1][6],'v1');
assert.equal(banks[location.bank][location.pad],5);
const restored=Collection.normalize(banks,session.markers.length-1,variants.map(v=>v.id));
assert.equal(restored[1][6],'v1','variants survive session restore');
assert.equal(restored[0][5],13,'requested Bank A Pad 6 retains Chop 14');
assert.equal(restored[0][11],22,'requested Bank A Pad 12 retains Chop 23');
assert.equal(restored[0][12],18,'requested Bank A Pad 13 retains Chop 19');
assert.equal(restored[0][13],19,'requested Bank A Pad 14 retains Chop 20');

const rate=8000,duration=23;
const samples=Float32Array.from({length:rate*duration},(_,i)=>Math.sin(i/18)*.2);
const audio={sampleRate:rate,numberOfChannels:1,length:samples.length,getChannelData(){return samples;}};
// A small independent set exercises export without the whole 3:44 source.
const localMarkers=[0,3,6,9];
const localVariants=[];
const full3=Variants.copyRange(0,localMarkers,localVariants);
const halves3=Variants.splitRange(full3.id,localMarkers,localVariants);
const bank=[Collection.emptyBank()];
bank[0][0]=0;bank[0][1]=full3.id;bank[0][2]=halves3[0].id;bank[0][3]=halves3[1].id;
const kit=Export.makeCollectionKit(audio,localMarkers,bank,[],{[full3.id]:{pitch:2}},
  {name:'Independent Copy Test',bpm:90,variants:localVariants});
assert.equal(kit.count,6,'3 originals and 3 independent copies should export');
assert(kit.entries.some(e=>e.name==='chops/COPY_v1_Independent_Copy_Test.wav'));
assert(kit.entries.some(e=>e.name==='banks/Bank_A/pad_02_copy_v1.wav'));
const map=JSON.parse(kit.entries.find(e=>e.name==='collection_map.json').data);
assert(map.chops.some(c=>c.variantId==='v1'&&c.startSeconds===0&&c.endSeconds===3));
assert(map.banks[0].pads.some(p=>p.pad===3&&p.variantId==='v2'));
const dest=path.join(__dirname,'test_copy_export.zip');
kit.blob.arrayBuffer().then(buffer=>{
  fs.writeFileSync(dest,Buffer.from(buffer));
  console.log(JSON.stringify({passed:true,originalPreserved:true,variants:variants.length,kitChops:kit.count,archive:dest}));
}).catch(e=>{console.error(e);process.exitCode=1;});
