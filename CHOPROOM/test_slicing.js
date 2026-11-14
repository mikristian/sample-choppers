const assert = require('node:assert/strict');
const fs = require('node:fs');
const {findCandidates, markersForPercent} = require('./slicing.js');

function syntheticAudio() {
  const sampleRate = 22050, length = sampleRate * 9;
  const left = new Float32Array(length);
  for (const [j, seconds] of [0.6,1.5,2.4,3.3,4.2,5.1,6.0,6.9,7.8].entries()) {
    const start = Math.round(seconds * sampleRate);
    for (let i=0;i<5000;i++)left[start+i] = (0.8-j*0.04)*Math.exp(-i/1800)*Math.sin(2*Math.PI*i/34);
  }
  return {sampleRate,length,numberOfChannels:1,getChannelData(){return left;}};
}

const audio=syntheticAudio(),candidates=findCandidates(audio,0,9);
assert(candidates.length>=7,'Expected detectable synthetic onsets');
const levels=[0,10,25,50,75,100];
const markers=levels.map(p=>markersForPercent(candidates,p));
assert.equal(markers[0].length,0,'0% means no automatically generated cuts');
assert(markers[3].length>markers[1].length,'Increasing percentage adds cuts');
assert(markers[5].length>markers[3].length,'100% adds more cuts than 50%');
assert(markers.every(m=>m.length<=15&&m.every((v,i)=>i===0||v>m[i-1])));
assert(markers.every((m,i)=>i===0||m.length>=markers[i-1].length));
for(let i=1;i<markers.length;i++)assert(markers[i-1].every(t=>markers[i].includes(t)),'Cuts remain as sensitivity increases');

console.log(JSON.stringify({syntheticCandidates:candidates.length,syntheticCounts:Object.fromEntries(levels.map((p,i)=>[p,markers[i].length])),passed:true}));
