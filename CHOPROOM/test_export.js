const assert=require('node:assert/strict');
const fs=require('node:fs');
const path=require('node:path');
const API=require('./export.js');

const rate=44100;
const channels=[
  Float32Array.from({length:rate*3},(_,i)=>Math.sin(i*440*2*Math.PI/rate)*.45),
  Float32Array.from({length:rate*3},(_,i)=>Math.sin(i*660*2*Math.PI/rate)*.3)
];
const audio={sampleRate:rate,length:rate*3,numberOfChannels:2,getChannelData(i){return channels[i];}};
const markers=Array.from({length:17},(_,i)=>3*i/16);
const padMap=Array.from({length:16},(_,i)=>i);
const settings={0:{pitch:3,fadeMs:8,reverse:true},1:{pitch:-3,fadeMs:5,reverse:false}};
const out=API.makeKit(audio,markers,padMap,settings,{name:'Test Session',bpm:92,events:[]});
assert.equal(out.count,16);
assert.equal(out.entries.filter(e=>e.name.endsWith('.wav')).length,16);
assert(out.blob.size>100000);
const wav=out.entries[0].data;
assert.equal(Buffer.from(wav.subarray(0,4)).toString(),'RIFF');
assert.equal(Buffer.from(wav.subarray(8,12)).toString(),'WAVE');
assert.equal(new DataView(wav.buffer).getUint16(22,true),2);
assert.equal(new DataView(wav.buffer).getUint32(24,true),rate);
assert.equal(new DataView(wav.buffer).getUint16(34,true),16);
const preview=out.entries.find(e=>e.name==='pad_reference.mid').data;
assert.equal(Buffer.from(preview.subarray(0,4)).toString(),'MThd');
assert.equal(Buffer.from(preview.subarray(14,18)).toString(),'MTrk');
assert.equal(API.midiNoteName(36),'C1');
assert.equal(API.midiNoteName(51),'D#2');
assert.equal(out.regions[15].midi,51);
const sfz=out.entries.find(e=>e.name==='chops.sfz').data;
assert(sfz.includes('key=36')&&sfz.includes('key=51'));
const file=path.join(__dirname,'test_choproom_kit.zip');
out.blob.arrayBuffer().then(buf=>{
  fs.writeFileSync(file,Buffer.from(buf));
  console.log(JSON.stringify({passed:true,wavSampleRate:rate,slices:out.count,zipBytes:out.blob.size,testZip:file}));
}).catch(err=>{console.error(err);process.exitCode=1;});
