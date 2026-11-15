import * as THREE from 'three';
import { OrbitControls } from './vendor/OrbitControls.js';
import { RoundedBoxGeometry } from './vendor/RoundedBoxGeometry.js';

// Independent virtual instrument. No Native Instruments firmware, library or UI assets.
const $ = (id) => document.getElementById(id);
const PAD_COLORS = ['#f5a250','#ffaa55','#ef9d48','#f4ac5e','#5bb9e7','#5ebfe4','#61bfe1','#71c2ed','#ee629f','#e85f9d','#e76ca6','#ed6ea9','#51d0a7','#4bcbab','#66d9af','#61d6ae'];
const PAD_NAMES = ['KICK 01','KICK 02','SNARE 01','CLAP 01','CLOSED HAT','OPEN HAT','SHAKER','RIMSHOT','TOM LOW','TOM HIGH','PERC 01','PERC 02','BASS C','BASS G','CHORD','TEXTURE'];
const KEY_MAP = '1234qwerasdfzxcv';
const state = {
  selected:0, bpm:124, swing:0, volume:.78, page:0, keyboard:false,
  playing:false, recording:false, currentStep:0, nextStep:0, nextTime:0,
  pads: PAD_NAMES.map((name,index)=>({name,buffer:null,source:'SYNTHESIZED',gain:1,pan:0,pitch:0,attack:4,release:250,filter:20000,start:0,end:1,index})),
  pattern:Array.from({length:16},()=>Array(32).fill(false)),
};
for (const step of [0,8,16,24]) state.pattern[0][step]=true;
for (const step of [4,12,20,28]) state.pattern[2][step]=true;
for (let s=0;s<32;s+=2) state.pattern[4][s]=true;
for (const step of [7,15,23,31])state.pattern[5][step]=true;

function status(msg){$('statusLine').textContent=msg;}
function clamp(x,a,b){return Math.max(a,Math.min(b,x));}
function shortTime(t){const minutes=Math.floor(t/60),seconds=Math.floor(t%60),milliseconds=Math.floor((t%1)*1000);return `${String(minutes).padStart(2,'0')}:${String(seconds).padStart(2,'0')}.${String(milliseconds).padStart(3,'0')}`;}

let audioCtx, master, recorderDestination, mediaRecorder, recordedChunks=[], noiseBuffer;
async function ensureAudio(){
  if (!audioCtx){
    const Ctx=window.AudioContext||window.webkitAudioContext;
    if(!Ctx){status('Web Audio unavailable in this browser.');return false;}
    audioCtx=new Ctx({latencyHint:'interactive'});
    master=audioCtx.createGain(); master.gain.value=state.volume*.76;
    master.connect(audioCtx.destination);
    recorderDestination=audioCtx.createMediaStreamDestination();master.connect(recorderDestination);
    noiseBuffer=audioCtx.createBuffer(1,Math.floor(audioCtx.sampleRate*1.8),audioCtx.sampleRate);
    const arr=noiseBuffer.getChannelData(0);let seed=1337;for(let i=0;i<arr.length;i++){seed=(1664525*seed+1013904223)>>>0;arr[i]=(seed/2147483648-1)*.9;}
  }
  if(audioCtx.state==='suspended')await audioCtx.resume();
  $('audioEnable').textContent='AUDIO ENABLED ✓';return true;
}

function createVoice(pad,at,velocity=1){
  if(!audioCtx)return;
  const sound=state.pads[pad];const when=Math.max(at??audioCtx.currentTime,audioCtx.currentTime);
  const filter=audioCtx.createBiquadFilter();filter.type='lowpass';filter.frequency.value=clamp(sound.filter,150,20000);
  const amp=audioCtx.createGain();amp.gain.value=0;
  const pan=audioCtx.createStereoPanner();pan.pan.value=sound.pan;
  filter.connect(amp);amp.connect(pan);pan.connect(master);
  const start=Math.max(0,Math.min(sound.start,.95)),end=Math.min(1,Math.max(sound.end,start+.01));
  let duration=.26;
  const vol=clamp(sound.gain*velocity,0,1.5),attack=Math.max(.002,sound.attack/1000);
  if(sound.buffer){
    const src=audioCtx.createBufferSource();src.buffer=sound.buffer;
    src.playbackRate.value=Math.pow(2,sound.pitch/12);
    const raw=src.buffer.duration;
    duration=Math.min(120,(end-start)*raw/src.playbackRate.value);
    const attackTime=Math.min(attack,duration*.4);
    const releaseTime=Math.min(sound.release/1000,duration-attackTime);
    amp.gain.setValueAtTime(.0001,when);
    amp.gain.linearRampToValueAtTime(vol,when+attackTime);
    amp.gain.setValueAtTime(vol,when+Math.max(attackTime,duration-releaseTime));
    amp.gain.linearRampToValueAtTime(.0001,when+duration);
    src.connect(filter);src.start(when,raw*start,raw*(end-start));src.stop(when+duration+.05);
  }else{
    const kind=pad%16;
    const isNoise=[2,3,4,5,6,7,10,11,15].includes(kind);
    duration = [0,1,12,13,14].includes(kind) ? .52 : isNoise?.12:.38;
    if([4,5,6].includes(kind))duration=kind===5?.41:.09;
    if(isNoise){
      const n=audioCtx.createBufferSource();n.buffer=noiseBuffer;
      const f=audioCtx.createBiquadFilter();f.type=kind===2||kind===3?'bandpass':'highpass';f.frequency.value=kind===2?1400:kind===3?1100:kind===15?700:4500;
      n.connect(f);f.connect(filter);n.start(when);n.stop(when+duration+.01);
    }
    if(!isNoise || [0,1,8,9,12,13,14].includes(kind)){
      const o=audioCtx.createOscillator();o.type=kind===14?'triangle':kind>11?'sawtooth':'sine';
      const base=[140,95,178,95,210,245,320,420,130,190,170,220,65,98,261.6,80][kind]*Math.pow(2,sound.pitch/12);
      o.frequency.setValueAtTime(base*(kind<2?2.6:1),when);
      o.frequency.exponentialRampToValueAtTime(Math.max(28,base*.45),when+Math.min(.16,duration));
      o.connect(filter);o.start(when);o.stop(when+duration+.01);
    }
    amp.gain.setValueAtTime(.0001,when);
    amp.gain.exponentialRampToValueAtTime(Math.max(.001,vol*.7),when+Math.min(attack,duration*.4));
    amp.gain.exponentialRampToValueAtTime(.0001,when+duration);
  }
  window.setTimeout(()=>{try{filter.disconnect();amp.disconnect();pan.disconnect();}catch{}},Math.round((when-audioCtx.currentTime+duration+.2)*1000));
}

function visualPad(pad){
  if(padLights[pad]){
    const material=padLights[pad].material;material.emissiveIntensity=1.8;
    window.setTimeout(()=>{material.emissiveIntensity=.26;},150);
  }
  const cell=document.querySelector(`.seq-title[data-pad="${pad}"]`);
  if(cell){cell.style.filter='brightness(1.7)';window.setTimeout(()=>cell.style.filter='',160);}
}
function triggerPad(pad,at=null,velocity=.9,fromSequence=false){
  if(pad<0||pad>15)return;
  if(!audioCtx){void ensureAudio().then(()=>{if(audioCtx)createVoice(pad,audioCtx.currentTime,velocity)});}
  else createVoice(pad,at,velocity);
  if(!fromSequence){selectPad(pad);if(state.recording&&state.playing){state.pattern[pad][state.currentStep]=true;renderSequencer();}}
  visualPad(pad);
}

const lookAhead=.13;let schedulerId=0;
function schedule(){
  if(!state.playing||!audioCtx)return;
  while(state.nextTime<audioCtx.currentTime+lookAhead){
    const step=state.nextStep;
    const when=state.nextTime+(step%2?(.6/state.bpm)*(state.swing/100):0);
    for(let p=0;p<16;p++)if(state.pattern[p][step])triggerPad(p,when,.86,true);
    const timeToVisual=Math.max(0,(when-audioCtx.currentTime)*1000);
    window.setTimeout(()=>{
      if(!state.playing)return;
      state.currentStep=step;paintPlayhead();refreshScreens();
      $('beatCounter').textContent=`01.${Math.floor(step/4)%4+1}.${step%4+1}`;
    },timeToVisual);
    state.nextStep=(step+1)%32;
    state.nextTime+=60/state.bpm/4;
  }
}
async function togglePlay(){
  if(state.playing){stop();return;}
  if(!await ensureAudio())return;
  state.playing=true;state.nextStep=state.currentStep;state.nextTime=audioCtx.currentTime+.04;
  $('playBtn').classList.add('active');$('playBtn').textContent='❚❚';
  schedulerId=window.setInterval(schedule,25);schedule();status(`Playing at ${state.bpm} BPM`);
}
function stop(){
  state.playing=false;window.clearInterval(schedulerId);schedulerId=0;
  state.recording=false;$('recordBtn').classList.remove('record');
  $('playBtn').classList.remove('active');$('playBtn').textContent='▶';
  state.currentStep=0;paintPlayhead();$('beatCounter').textContent='01.1.1';status('Transport stopped.');
}

function persist(){
  try{localStorage.setItem('maschine-lab.v1',JSON.stringify({bpm:state.bpm,swing:state.swing,volume:state.volume,pattern:state.pattern,pads:state.pads.map(p=>Object.fromEntries(['name','pitch','attack','release','filter','gain','pan','start','end'].map(key=>[key,p[key]])))}));}catch{}
}
function restore(){try{
  const d=JSON.parse(localStorage.getItem('maschine-lab.v1')||'null');if(!d)return;
  if(d.pattern?.length===16&&d.pattern.every(p=>p.length===32))state.pattern=d.pattern.map(row=>row.map(Boolean));
  state.bpm=clamp(Number(d.bpm)||124,40,240);state.swing=clamp(Number(d.swing)||0,0,65);state.volume=clamp(Number(d.volume)||.78,0,1);
  d.pads?.slice(0,16).forEach((p,i)=>{for(const key of ['name','pitch','attack','release','filter','gain','pan','start','end'])if(p[key]!==undefined)state.pads[i][key]=p[key];});
}catch(error){console.warn('Could not restore project',error);}}
restore();

function renderSequencer(){
  const parent=$('sequencer');parent.replaceChildren();
  const row=document.createElement('div');row.className='seq-row';
  const head=document.createElement('div');head.className='seq-num';head.textContent='PAD / STEP';row.append(head);
  for(let s=0;s<16;s++){const n=document.createElement('div');n.className='seq-num';n.textContent=String(state.page*16+s+1).padStart(2,'0');row.append(n);}parent.append(row);
  for(let p=0;p<16;p++){
    const r=document.createElement('div');r.className='seq-row';r.style.setProperty('--pad-color',PAD_COLORS[p]);
    const label=document.createElement('button');label.className='seq-title'+(state.selected===p?' selected':'');label.dataset.pad=p;
    label.innerHTML=`<i></i><span>${String(p+1).padStart(2,'0')} ${escapeHTML(state.pads[p].name)}</span>`;
    label.onclick=()=>{selectPad(p);void ensureAudio().then(()=>triggerPad(p));};r.append(label);
    for(let s=0;s<16;s++){
      const step=state.page*16+s;
      const button=document.createElement('button');button.className='seq-step'+(state.pattern[p][step]?' active':'');
      button.title=`${state.pads[p].name}: Step ${step+1}`;button.setAttribute('aria-label',button.title);button.dataset.step=step;button.dataset.pad=p;
      button.onclick=()=>{state.pattern[p][step]=!state.pattern[p][step];button.classList.toggle('active');persist();};r.append(button);
    }parent.append(r);
  }
  paintPlayhead();
}
function escapeHTML(v){return String(v).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));}
function paintPlayhead(){
  document.querySelectorAll('.seq-step.playhead').forEach(b=>b.classList.remove('playhead'));
  document.querySelectorAll(`.seq-step[data-step="${state.currentStep}"]`).forEach(b=>b.classList.add('playhead'));
}

function selectPad(p){
  state.selected=p;const pad=state.pads[p];$('selectedPadNum').textContent=String(p+1).padStart(2,'0');$('padName').textContent=pad.name;
  $('padColor').style.background=PAD_COLORS[p];$('activePadDisplay').textContent=String(p+1).padStart(2,'0')+' / '+pad.name;
  $('sourceName').textContent=pad.source==='SYNTHESIZED'?'SYNTHESIZED / '+pad.name:pad.source;
  $('sourceMeta').textContent=pad.buffer?`${pad.buffer.numberOfChannels} CH · ${pad.buffer.sampleRate} HZ · ${pad.buffer.duration.toFixed(2)} SEC`:'GENERATED IN BROWSER · NO SAMPLE NEEDED';
  for(const key of ['pitch','attack','release','filter','pan'])$(key).value=pad[key];
  $('padGain').value=pad.gain*100;$('sampleStart').value=pad.start*100;$('sampleEnd').value=pad.end*100;
  updateLabels();drawWaveform();renderSequencer();refreshScreens();
}
function updateLabels(){const p=state.pads[state.selected];
  $('pitchOutput').textContent=`${p.pitch>0?'+':''}${p.pitch} ST`;$('attackOutput').textContent=`${p.attack} MS`;
  $('releaseOutput').textContent=`${p.release} MS`;$('filterOutput').textContent=p.filter>=20000?'OPEN':Math.round(p.filter)+' HZ';
  $('padGainOutput').textContent=Math.round(p.gain*100)+'%';$('panOutput').textContent=p.pan===0?'CENTER':p.pan<0?`${Math.round(-p.pan*100)}% LEFT`:`${Math.round(p.pan*100)}% RIGHT`;
  const length=p.buffer?.duration||.5;$('startLabel').textContent=shortTime(length*p.start);$('endLabel').textContent=shortTime(length*p.end);
}
function waveformDraw(ctx,width,height,pad,selectedColor){
  const w=width,h=height;ctx.clearRect(0,0,w,h);ctx.fillStyle='#17212a';ctx.fillRect(0,0,w,h);
  ctx.strokeStyle='#2c3742';ctx.lineWidth=1;
  for(let x=0;x<w;x+=w/12){ctx.beginPath();ctx.moveTo(x,0);ctx.lineTo(x,h);ctx.stroke();}
  ctx.beginPath();ctx.moveTo(0,h/2);ctx.lineTo(w,h/2);ctx.stroke();
  const data=pad.buffer?.getChannelData(0);
  ctx.strokeStyle=selectedColor;ctx.lineWidth=2;ctx.beginPath();
  for(let x=0;x<w;x+=2){
    let max=0;
    if(data){const a=Math.floor(x/w*data.length),b=Math.min(data.length,Math.floor((x+2)/w*data.length));for(let k=a;k<b;k+=Math.max(1,Math.floor((b-a)/5)))max=Math.max(max,Math.abs(data[k]));}
    else{const t=x/w;max=(.09+.78*Math.exp(-t*8))*(.6+.4*Math.abs(Math.sin(t*130)));}
    ctx.moveTo(x,h/2-max*h*.43);ctx.lineTo(x,h/2+max*h*.43);
  }ctx.stroke();
  ctx.fillStyle='#f9946540';ctx.fillRect(0,0,w*pad.start,h);ctx.fillRect(w*pad.end,0,w*(1-pad.end),h);
  ctx.strokeStyle='#ffcba2';ctx.beginPath();ctx.moveTo(w*pad.start,0);ctx.lineTo(w*pad.start,h);ctx.moveTo(w*pad.end,0);ctx.lineTo(w*pad.end,h);ctx.stroke();
}
function drawWaveform(){const canvas=$('waveform');const ctx=canvas.getContext('2d');waveformDraw(ctx,canvas.width,canvas.height,state.pads[state.selected],PAD_COLORS[state.selected]);}

async function loadFiles(files,firstPad=0){
  if(!files.length)return;
  if(!await ensureAudio())return;
  const list=[...files].filter(f=>/\.(wav|aiff?|mp3|ogg|flac|m4a)$/i.test(f.name)||f.type.startsWith('audio/'));
  const ordered=list.sort((a,b)=>{
    const na=Number(a.name.match(/(?:pad[_\s-]*|^)(\d{1,2})(?:\D|$)/i)?.[1]);
    const nb=Number(b.name.match(/(?:pad[_\s-]*|^)(\d{1,2})(?:\D|$)/i)?.[1]);
    return (Number.isFinite(na)?na:999)-(Number.isFinite(nb)?nb:999)||a.name.localeCompare(b.name,undefined,{numeric:true});
  });
  let count=0;for(const file of ordered.slice(0,16-firstPad))try{
    const buf=await audioCtx.decodeAudioData(await file.arrayBuffer());const p=state.pads[firstPad+count];p.buffer=buf;p.source=file.name;p.name=file.name.replace(/\.[^.]+$/,'').toUpperCase().slice(0,19);p.start=0;p.end=1;count++;
  }catch(err){console.warn('Cannot decode',file.name,err);}
  selectPad(firstPad);status(`Loaded ${count} audio sample${count===1?'':'s'} into pad${count===1?'':'s'} ${firstPad+1}–${firstPad+count}.`);persist();
}
async function sliceCurrent(){
  const source=state.pads[state.selected].buffer;
  if(!source){status('Load a full WAV/MP3 on the selected pad before slicing.');return;}
  const sourceName=state.pads[state.selected].source;
  for(let p=0;p<16;p++){const target=state.pads[p];target.buffer=source;target.source=`SLICE ${p+1} / ${sourceName}`;target.name=`SLICE ${String(p+1).padStart(2,'0')}`;target.start=p/16;target.end=(p+1)/16;}
  selectPad(0);status('Sliced source into 16 equal playable sample regions.');persist();
}

function writeVar(n){let bytes=[n&127];while(n>127){n=Math.floor(n/128);bytes.unshift((n&127)|128);}return bytes;}
function midiExport(){
  const ppq=480;const events=[];const tempo=Math.round(60000000/state.bpm);
  events.push({tick:0,data:[0xff,0x51,0x03,(tempo>>16)&255,(tempo>>8)&255,tempo&255]});
  for(let pad=0;pad<16;pad++)for(let s=0;s<32;s++)if(state.pattern[pad][s]){
    const start=s*120+(s%2?Math.round(60*state.swing/100):0),end=start+85;
    events.push({tick:start,data:[0x99,pad+36,100]});events.push({tick:end,data:[0x89,pad+36,0]});
  }
  events.sort((a,b)=>a.tick-b.tick||((a.data[0]===0x89?-1:1)-(b.data[0]===0x89?-1:1)));
  const track=[];let prev=0;for(const e of events){track.push(...writeVar(e.tick-prev),...e.data);prev=e.tick;}track.push(0,255,47,0);
  const n32=n=>[(n>>>24)&255,(n>>>16)&255,(n>>>8)&255,n&255],n16=n=>[(n>>8)&255,n&255];
  const bytes=[77,84,104,100,0,0,0,6,...n16(0),...n16(1),...n16(ppq),77,84,114,107,...n32(track.length),...track];
  download(new Blob([new Uint8Array(bytes)],{type:'audio/midi'}),'maschine-lab-pattern.mid');status('Exported 2-bar drum pattern to Standard MIDI File (notes 36–51).');
}
function download(blob,name){const url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;a.download=name;a.click();window.setTimeout(()=>URL.revokeObjectURL(url),1000);}

// THREE.JS: geometry in centimeters; reference envelope 322 x 301 x 51 mm.
const canvas=$('threeCanvas');const renderer=new THREE.WebGLRenderer({canvas,antialias:true,alpha:true,powerPreference:'high-performance'});
renderer.setPixelRatio(Math.min(devicePixelRatio||1,2));renderer.outputColorSpace=THREE.SRGBColorSpace;
renderer.toneMapping=THREE.ACESFilmicToneMapping;renderer.toneMappingExposure=1.5;
renderer.shadowMap.enabled=true;renderer.shadowMap.type=THREE.PCFSoftShadowMap;
const scene=new THREE.Scene();const camera=new THREE.PerspectiveCamera(42,1,.1,450);
const controls=new OrbitControls(camera,canvas);controls.enableDamping=true;controls.dampingFactor=.07;controls.maxPolarAngle=Math.PI*.48;controls.minDistance=26;controls.maxDistance=210;
const hemi=new THREE.HemisphereLight(0xf0f4ff,0x26313a,2.2);scene.add(hemi);
const keyLight=new THREE.DirectionalLight(0xffffff,4);keyLight.position.set(-24,80,25);keyLight.castShadow=true;keyLight.shadow.mapSize.set(2048,2048);keyLight.shadow.camera.left=-80;keyLight.shadow.camera.right=80;keyLight.shadow.camera.top=80;keyLight.shadow.camera.bottom=-80;scene.add(keyLight);
const rimLight=new THREE.DirectionalLight(0x6580bd,1.6);rimLight.position.set(30,30,-30);scene.add(rimLight);
const ground=new THREE.Mesh(new THREE.PlaneGeometry(260,260),new THREE.MeshStandardMaterial({color:0x161a20,roughness:.85,metalness:.2}));ground.rotation.x=-Math.PI/2;ground.position.y=-3.3;ground.receiveShadow=true;scene.add(ground);
const grid=new THREE.GridHelper(220,36,0x242a32,0x222832);grid.position.y=-3.28;grid.material.transparent=true;grid.material.opacity=.26;scene.add(grid);
const mats={
  metal:new THREE.MeshStandardMaterial({color:0x16191d,metalness:.58,roughness:.55}),
  side:new THREE.MeshStandardMaterial({color:0x0b0d10,metalness:.52,roughness:.54}),
  panel:new THREE.MeshStandardMaterial({color:0x202328,metalness:.35,roughness:.65}),
  black:new THREE.MeshStandardMaterial({color:0x0c0e12,metalness:.15,roughness:.53}),
  button:new THREE.MeshStandardMaterial({color:0x34383d,roughness:.7}),
  white:new THREE.MeshStandardMaterial({color:0xc8d0d6,roughness:.4}),
  keyWhite:new THREE.MeshStandardMaterial({color:0xf8f8ec,roughness:.41}),
  keyBlack:new THREE.MeshStandardMaterial({color:0x13171e,roughness:.46}),
  orange:new THREE.MeshStandardMaterial({color:0xf68c52,emissive:0xe57b36,emissiveIntensity:.45}),
};
const hitTargets=[],padLights=[],screenTextures=[],padMeshes=[];let maschineGroup,keyboardGroup;
function shape(group,w,h,d,x,y,z,mat=mats.panel,r=.12){const m=new THREE.Mesh(new RoundedBoxGeometry(w,h,d,3,Math.min(r,h*.48)),mat);m.position.set(x,y,z);m.castShadow=true;group.add(m);return m;}
function labelTex(text,color='#8c989f',fontSize=100){
  const c=document.createElement('canvas');c.width=768;c.height=130;const ctx=c.getContext('2d');ctx.fillStyle=color;ctx.font=`700 ${fontSize}px Arial`;ctx.textAlign='center';ctx.textBaseline='middle';ctx.fillText(String(text).toUpperCase(),384,67,750);
  const t=new THREE.CanvasTexture(c);t.colorSpace=THREE.SRGBColorSpace;return t;
}
function label(group,text,x,z,w=3.0,h=.42,color='#8a919b',size=74,y=2.71){
  const m=new THREE.Mesh(new THREE.PlaneGeometry(w,h),new THREE.MeshBasicMaterial({map:labelTex(text,color,size),transparent:true,side:THREE.DoubleSide,depthWrite:false}));m.rotation.x=-Math.PI/2;m.position.set(x,y,z);group.add(m);return m;
}
function addButton(g,x,z,w,d,name,action,opts={}){
  const m=shape(g,w,.21,d,x,2.68,z,opts.mat||mats.button,.12);
  const top=label(g,name,x,z,w*.94,Math.min(.44,d*.6),opts.color||'#8d989e',name.length>12?36:name.length>8?47:63,2.82);
  if(action)hitTargets.push({mesh:m,action,label:name,group:g});
  return m;
}
function knob(g,x,z,labelText,action,r=.95){
  const body=new THREE.Mesh(new THREE.CylinderGeometry(r*.85,r*.96,.43,40),mats.side);body.position.set(x,2.82,z);body.castShadow=true;g.add(body);
  const ring=new THREE.Mesh(new THREE.TorusGeometry(r*.88,.065,7,44),mats.button);ring.rotation.x=-Math.PI/2;ring.position.set(x,3.04,z);g.add(ring);
  const dot=shape(g,.12,.065,.37,x,3.10,z-r*.38,mats.white,.025);
  if(labelText)label(g,labelText,x,z+r+ .32,2.35,.34,'#78838b',46,2.72);
  hitTargets.push({mesh:body,action,label:labelText||'ENCODER',group:g});return{body,dot};
}
function makeScreen(g,x,z,index){
  shape(g,9.4,.11,5.6,x,2.64,z,mats.black,.14);
  const c=document.createElement('canvas');c.width=480;c.height=272;
  const t=new THREE.CanvasTexture(c);t.colorSpace=THREE.SRGBColorSpace;
  const screen=new THREE.Mesh(new THREE.PlaneGeometry(8.95,5.08),new THREE.MeshBasicMaterial({map:t,toneMapped:false}));screen.rotation.x=-Math.PI/2;screen.position.set(x,2.712,z);g.add(screen);
  screenTextures[index]={canvas:c,texture:t};
  return screen;
}
function buildMaschine(){
  const g=new THREE.Group();scene.add(g);maschineGroup=g;
  shape(g,32.2,4.78,30.1,0,0,0,mats.side,.75);
  shape(g,32.1,.35,29.98,0,2.42,0,mats.metal,.48);
  // Rear dual color displays, eight dynamically mapped touch encoders.
  makeScreen(g,1.45,-10.3,0);makeScreen(g,10.83,-10.3,1);
  for(let i=0;i<8;i++)knob(g,-2.55+i*2.35,-6.05,`ENC ${i+1}`,()=>{state.pads[state.selected].pitch=clamp(state.pads[state.selected].pitch+(i<4?1:-1),-24,24);$('pitch').value=state.pads[state.selected].pitch;updateLabels();refreshScreens();persist();},.64);
  for(let i=0;i<8;i++)addButton(g,-2.58+i*2.36,-14.02,1.86,.66,`F${i+1}`,()=>{state.page=i%2;renderSequencer();},{});
  const left=[['CHANNEL','PLUGIN'],['BROWSE','SAMPLING'],['ARRANGER','MIXER'],['FILE','SETTINGS'],['PAD MODE','KEYBOARD'],['CHORDS','STEP']];
  left.forEach((row,r)=>row.forEach((text,c)=>addButton(g,-14.05+c*3.35,-13.25+r*1.69,2.92,1.12,text,()=>handleHardware(text),{color:'#7d8793'})));
  knob(g,-12.40,-1.15,'SELECT',()=>{selectPad((state.selected+1)%16)},2);
  addButton(g,-14.35,2.45,3.1,.95,'SWING',()=>{$('swing').value=Math.min(65,state.swing+5);$('swing').dispatchEvent(new Event('input'));});
  addButton(g,-10.92,2.45,3.1,.95,'NOTE REPEAT',()=>triggerPad(state.selected));
  addButton(g,-14.35,4.2,3.1,.95,'TEMPO',()=>{$('bpm').value=state.bpm+1;$('bpm').dispatchEvent(new Event('change'));});
  addButton(g,-10.92,4.2,3.1,.95,'LOCK',()=>status('Parameter-lock emulator: adjust selected pad using the sound editor.'));
  for(let i=0;i<4;i++)addButton(g,-14.45+(i%2)*4.65,6.35+Math.floor(i/2)*1.45,4.2,1.03,['REC','ERASE','PLAY','STOP'][i],()=>handleHardware(['REC','ERASE','PLAY','STOP'][i]));
  // Smart Strip: horizontally placed expressive touch control.
  shape(g,11.3,.09,1.0,-10,2.7,10.35,mats.black,.16);
  for(let i=0;i<19;i++)shape(g,.16,.05,.66,-15.25+i*.59,2.76,10.35,new THREE.MeshStandardMaterial({color:i<9?0x364044:0x757f88,emissive:i<9?0x0c1c28:0x1f262f}),.03);
  label(g,'SMART STRIP',-10.0,11.4,8,.35,'#707d88',52);
  // Group buttons adjacent to pads, plus 4x4 velocity pads.
  const groupLabels=['SCENE','PATTERN','EVENTS','VARIATION','DUPLICATE','SELECT','MUTE','SOLO'];
  groupLabels.forEach((t,i)=>addButton(g,1.95,-1.0+i*1.74,2.43,1.12,t,()=>handleHardware(t)));
  for(let p=0;p<16;p++){
    const row=Math.floor(p/4),col=p%4,x=5.6+col*2.95,z=-.68+row*3.35;
    shape(g,2.85,.09,3.18,x,2.68,z,mats.black,.25);
    const m=shape(g,2.68,.45,2.98,x,2.94,z,new THREE.MeshStandardMaterial({color:new THREE.Color(PAD_COLORS[p]).multiplyScalar(.74),roughness:.57,metalness:.03,emissive:new THREE.Color(PAD_COLORS[p]),emissiveIntensity:.26}),.22);
    padLights[p]=m;padMeshes[p]=m;hitTargets.push({mesh:m,action:()=>triggerPad(p),label:`PAD ${p+1} · ${PAD_NAMES[p]}`,group:g});
    label(g,String(p+1),x-.88,z-1.05,.48,.34,'#37282c',70,3.22);
  }
  label(g,'MASCHINE',-8.7,13.27,9.2,.75,'#dce4e8',78);
  label(g,'+',-3.25,13.27,1.0,.7,'#f18d54',95);
  // Side/rear connection hints and four rubber feet.
  for(const x of [-13.4,13.4])for(const z of [-12.6,12.6])shape(g,2,.4,2,x,-2.58,z,mats.black,.3);
  return g;
}
const keyboardKeys=[];
function buildKeyboard(){
  const g=new THREE.Group();keyboardGroup=g;scene.add(g);g.position.set(0,0,43);
  const ly=-.5;shape(g,80.3,4.4,26.7,0,ly,0,mats.side,.55);
  shape(g,80,.22,26.4,0,1.72,0,mats.metal,.38);
  label(g,'IMPACT LX49',28.5,-10.7,14.0,1.05,'#d5dcde',80,1.93);
  label(g,'NEKTAR',-32.8,10.5,8.5,.72,'#8bb8ce',69,2.03);
  // 49 consecutive notes starting C3, 29 white keys and 20 accidentals.
  const whites=[],keyWidth=2.42,start=-35.9;
  for(let n=0;n<49;n++){if(![1,3,6,8,10].includes(n%12))whites.push(n);}
  whites.forEach((n,i)=>{
    const x=start+i*keyWidth;
    const mesh=shape(g,2.33,.8,14.0,x,2.16,5.22,mats.keyWhite,.1);keyboardKeys.push({mesh,note:48+n});
    hitTargets.push({mesh,action:()=>playKeyboardNote(48+n),label:`KEY ${midiName(48+n)}`,group:g});
  });
  for(let n=0;n<49;n++){
    if(![1,3,6,8,10].includes(n%12))continue;
    const earlier=whites.filter(i=>i<n).length-1;
    const x=start+(earlier+.50)*keyWidth;
    const mesh=shape(g,1.41,1.03,8.8,x,2.75,2.85,mats.keyBlack,.09);
    hitTargets.push({mesh,action:()=>playKeyboardNote(48+n),label:`BLACK KEY ${midiName(48+n)}`,group:g});
  }
  // Performance wheels.
  for(let i=0;i<2;i++){
    shape(g,1.83,.55,4.66,-37.6,2.28,1.20+i*5.3,mats.black,.25);
    const wheel=shape(g,.90,.6,2.5,-37.6,2.60,1.2+i*5.3,mats.button,.34);
    hitTargets.push({mesh:wheel,action:()=>{state.pads[state.selected].pitch=clamp(state.pads[state.selected].pitch+1,-24,24);selectPad(state.selected);},label:i?'MOD WHEEL':'PITCH WHEEL',group:g});
  }
  // 9 faders: physical slots and caps.
  for(let i=0;i<9;i++){
    const x=-27.0+i*3.3;
    shape(g,.35,.07,5.0,x,1.93,-8.2,mats.black,.10);
    const cap=shape(g,1.30,.34,.80,x,2.13,-8.15,mats.button,.12);
    hitTargets.push({mesh:cap,action:()=>{const p=state.selected;state.pads[p].gain=clamp(state.pads[p].gain+.1,0,1.5);selectPad(p);persist();},label:`FADER ${i+1}`,group:g});
  }
  for(let i=0;i<8;i++){
    const x=2.5+i*3.1;knob(g,x,-9.2,'',()=>{state.pads[state.selected].pitch=clamp(state.pads[state.selected].pitch+1,-24,24);selectPad(state.selected);},.63);
  }
  for(let i=0;i<8;i++){
    const x=19.5+(i%4)*4.4,z=-3.35+Math.floor(i/4)*3.65;
    const m=shape(g,3.58,.38,3.1,x,2.03,z,new THREE.MeshStandardMaterial({color:i%2?0x424751:0x454c57,emissive:new THREE.Color(PAD_COLORS[i]),emissiveIntensity:.05}),.14);
    hitTargets.push({mesh:m,action:()=>triggerPad(i),label:`LX49 PAD ${i+1}`,group:g});
  }
  const transport=['◀◀','▶','■','●','▶▶'];
  transport.forEach((name,i)=>addKeyboardButton(g,8+i*3.0,-3.55,name,()=>i===1?togglePlay():i===2?stop():i===3?toggleRecord():status('Transport navigation.')));
  const red=new THREE.MeshBasicMaterial({color:0xa72e2f});shape(g,71,.05,.11,0,1.86,-.96,red,.02);
  g.visible=false;return g;
}
function addKeyboardButton(g,x,z,name,action){
  const mat=name==='●'?mats.orange:mats.button;
  const m=shape(g,2.35,.20,1.35,x,2.02,z,mat,.12);
  label(g,name,x,z,2,.70,'#d3cfc9',70,2.17);hitTargets.push({mesh:m,action,label:`LX49 ${name}`,group:g});
}
function midiName(n){return ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'][n%12]+(Math.floor(n/12)-1);}
function playKeyboardNote(note){
  const selected=state.pads[state.selected];
  if(selected.buffer){
    const old=selected.pitch;selected.pitch=old+(note-60);triggerPad(state.selected);selected.pitch=old;
  }else triggerPad(note%16);
}
function handleHardware(labelText){
  switch(labelText){
    case 'PLAY':void togglePlay();break;case 'STOP':stop();break;case 'REC':toggleRecord();break;
    case 'ERASE':state.pattern[state.selected].fill(false);renderSequencer();persist();break;
    case 'SAMPLING':$('singleSample').click();break;case 'BROWSE':$('sampleFiles').click();break;
    case 'STEP':document.querySelector('.bottom-panel').scrollIntoView({block:'end'});break;
    case 'KEYBOARD':setKeyboard(!state.keyboard);break;
    case 'MIXER':setTab('mixer');break;
    case 'PLUGIN':setTab('shape');break;
    case 'PATTERN':state.page=state.page?0:1;updatePage();break;
    default:status(`${labelText}: selected. Use the inspector and step grid to edit the session.`);
  }
}

function refreshScreens(){
  for(let i=0;i<screenTextures.length;i++){
    const screen=screenTextures[i];if(!screen)continue;
    const {canvas,texture}=screen,ctx=canvas.getContext('2d');const w=canvas.width,h=canvas.height;
    ctx.fillStyle='#0c1422';ctx.fillRect(0,0,w,h);
    const grd=ctx.createLinearGradient(0,0,0,h);grd.addColorStop(0,'#183041');grd.addColorStop(1,'#101b2a');ctx.fillStyle=grd;ctx.fillRect(3,4,w-6,h-8);
    ctx.fillStyle='#d5e3eb';ctx.font='bold 14px monospace';ctx.fillText(i?'PATTERN   01':'SAMPLER / EDIT',16,23);
    ctx.fillStyle='#f09555';ctx.fillRect(0,30,w,2);
    if(!i){
      const pad=state.pads[state.selected];ctx.fillStyle='#f9aa66';ctx.font='bold 18px monospace';ctx.fillText(`${String(state.selected+1).padStart(2,'0')} ${pad.name}`,15,57);
      ctx.font='12px monospace';ctx.fillStyle='#97b3c4';ctx.fillText(pad.source.slice(0,36),15,76);
      ctx.save();ctx.translate(15,92);waveformDraw(ctx,448,140,pad,PAD_COLORS[state.selected]);ctx.restore();
      ctx.fillStyle='#a1b9c7';ctx.font='13px monospace';ctx.fillText(`START ${Math.round(pad.start*100)}%`,15,253);ctx.fillText(`END ${Math.round(pad.end*100)}%`,252,253);
    }else{
      ctx.fillStyle='#b4c8d3';ctx.font='13px monospace';ctx.fillText(`BPM ${state.bpm}     STEP ${state.currentStep+1} / 32`,17,54);
      for(let p=0;p<8;p++)for(let s=0;s<16;s++){
        const x=17+s*28,y=80+p*20;const active=state.pattern[p][state.page*16+s];
        ctx.fillStyle=active?PAD_COLORS[p]:'#263746';ctx.fillRect(x,y,23,14);
        if(s===state.currentStep%16 && Math.floor(state.currentStep/16)===state.page){ctx.strokeStyle='#e6f5ff';ctx.strokeRect(x-1,y-1,25,16);}
      }
      ctx.fillStyle='#a2b8c6';ctx.font='11px monospace';ctx.fillText('F1: PATTERN   F2: EDIT   F3: MIX   F4: PLAY',16,256);
    }
    texture.needsUpdate=true;
  }
}
buildMaschine();buildKeyboard();
function setKeyboard(visible){
  state.keyboard=visible;keyboardGroup.visible=visible;$('showKeyboard').checked=visible;
  $('keyboardTab').classList.toggle('selected',visible);$('maschineTab').classList.toggle('selected',!visible);
  $('deviceTitle').innerHTML=visible?'MASCHINE <span>+</span> / LX49':'MASCHINE <span>+</span>';
  $('deviceSubtitle').textContent=visible?'Dual-instrument setup · sampler + 49-key MIDI controller':'3D production controller · 16 expressive pads · dual displays';
  $('modelStatus').textContent=visible?'MASCHINE+ + NEKTAR IMPACT LX49':'MASCHINE+ / 322 × 301 × 51 MM';
  fitCamera();status(visible?'Showing both instruments with 49 playable keys.':'MASCHINE+ close-up ready.');
}
function fitCamera(top=false){
  const target=state.keyboard?new THREE.Vector3(0,0,20):new THREE.Vector3(0,0,0);
  controls.target.copy(target);camera.position.copy(state.keyboard?(top?new THREE.Vector3(0,110,20):new THREE.Vector3(74,94,109)):(top?new THREE.Vector3(0,54,.01):new THREE.Vector3(28,38,45)));
  camera.lookAt(target);controls.update();
}
fitCamera();
function resize(){
  const rect=canvas.getBoundingClientRect();if(rect.width<1||rect.height<1)return;
  renderer.setSize(rect.width,rect.height,false);camera.aspect=rect.width/rect.height;camera.updateProjectionMatrix();
}
new ResizeObserver(resize).observe(canvas);resize();
let lastFrame=0;function frame(ts){requestAnimationFrame(frame);controls.update();renderer.render(scene,camera);if(ts-lastFrame>100){lastFrame=ts;}}requestAnimationFrame(frame);
const raycaster=new THREE.Raycaster(),pointer=new THREE.Vector2();
function hovered(ev){
  const rect=canvas.getBoundingClientRect();pointer.x=((ev.clientX-rect.left)/rect.width)*2-1;pointer.y=-((ev.clientY-rect.top)/rect.height)*2+1;
  raycaster.setFromCamera(pointer,camera);
  const hits=raycaster.intersectObjects(hitTargets.filter(h=>h.group.visible).map(h=>h.mesh));
  if(!hits.length)return null;
  return hitTargets.find(h=>h.mesh===hits[0].object)||null;
}
let down;
canvas.addEventListener('pointermove',ev=>{const item=hovered(ev);$('hoverLabel').textContent=item?item.label:'DRAG TO ROTATE • CLICK TO PLAY';canvas.style.cursor=item?'pointer':'grab';});
canvas.addEventListener('pointerdown',ev=>{down={item:hovered(ev),x:ev.clientX,y:ev.clientY};});
canvas.addEventListener('pointerup',ev=>{if(!down)return;const moved=Math.hypot(ev.clientX-down.x,ev.clientY-down.y);if(down.item&&moved<7){down.item.action();}down=null;});

function setTab(tab){
  document.querySelectorAll('.inspector-tab').forEach(b=>b.classList.toggle('active',b.dataset.tab===tab));
  for(const which of ['sample','shape','mixer'])$(which+'Tab').classList.toggle('hidden',tab!==which);
}
function updatePage(){$('pageOne').classList.toggle('active',state.page===0);$('pageTwo').classList.toggle('active',state.page===1);renderSequencer();refreshScreens();}
function toggleRecord(){state.recording=!state.recording;$('recordBtn').classList.toggle('record',state.recording);if(state.recording&&!state.playing)void togglePlay();status(state.recording?'RECORD ARMED: Live pad hits are added to the playing 32-step pattern.':'Pattern record disabled.');}

// Controls and keyboard shortcuts.
$('audioEnable').onclick=()=>{void ensureAudio();};$('playBtn').onclick=()=>{void togglePlay();};$('stopBtn').onclick=stop;$('recordBtn').onclick=toggleRecord;
$('bpm').value=state.bpm;$('bpm').onchange=e=>{state.bpm=clamp(Number(e.target.value)||124,40,240);e.target.value=state.bpm;persist();refreshScreens();};
$('swing').value=state.swing;$('swingValue').textContent=state.swing+'%';$('swing').oninput=e=>{state.swing=Number(e.target.value);$('swingValue').textContent=state.swing+'%';persist();};
$('volume').value=state.volume*100;$('volume').oninput=e=>{state.volume=Number(e.target.value)/100;if(master)master.gain.setTargetAtTime(state.volume*.76,audioCtx.currentTime,.02);persist();};
$('pageOne').onclick=()=>{state.page=0;updatePage();};$('pageTwo').onclick=()=>{state.page=1;updatePage();};
$('clearBtn').onclick=()=>{state.pattern.forEach(r=>r.fill(false));renderSequencer();persist();refreshScreens();status('Pattern cleared (all 32 steps).');};
$('randomBtn').onclick=()=>{for(let p=0;p<16;p++)for(let s=0;s<32;s++)state.pattern[p][s]=Math.random()<(p<6?.21:.065);renderSequencer();persist();refreshScreens();status('New randomized 32-step rhythm generated.');};
$('showKeyboard').onchange=e=>setKeyboard(e.target.checked);$('keyboardTab').onclick=()=>setKeyboard(true);$('maschineTab').onclick=()=>setKeyboard(false);
$('fitView').onclick=()=>fitCamera();$('topView').onclick=()=>{fitCamera(true);$('topView').classList.add('active');$('perspectiveView').classList.remove('active');};
$('perspectiveView').onclick=()=>{fitCamera(false);$('perspectiveView').classList.add('active');$('topView').classList.remove('active');};
$('importSamplesBtn').onclick=()=>$('sampleFiles').click();$('importKitBtn').onclick=()=>$('kitFiles').click();
$('replacePadBtn').onclick=()=>$('singleSample').click();$('sliceBtn').onclick=()=>{void sliceCurrent();};
$('sampleFiles').onchange=e=>{const files=[...e.target.files];e.target.value='';void loadFiles(files,0);};
$('kitFiles').onchange=e=>{const files=[...e.target.files];e.target.value='';void loadFiles(files,0);};
$('singleSample').onchange=e=>{const files=[...e.target.files];e.target.value='';void loadFiles(files,state.selected);};
$('demoKitBtn').onclick=()=>{state.pads.forEach((p,i)=>{p.buffer=null;p.source='SYNTHESIZED';p.name=PAD_NAMES[i];p.start=0;p.end=1;});selectPad(0);status('Reset to built-in synthesized demo sounds.');};
$('saveProject').onclick=()=>{persist();download(new Blob([JSON.stringify({name:'MASCHINE LAB',bpm:state.bpm,pattern:state.pattern,pads:state.pads.map(({buffer,...rest})=>rest)},null,2)],{type:'application/json'}),'maschine-lab-project.json');status('Project metadata and steps saved. Imported audio must be reloaded after refresh.');};
$('exportProject').onclick=midiExport;
document.querySelectorAll('.inspector-tab').forEach(b=>b.onclick=()=>setTab(b.dataset.tab));
for(const key of ['pitch','attack','release','filter','pan','padGain','sampleStart','sampleEnd']){
  $(key).oninput=e=>{
    const p=state.pads[state.selected],val=Number(e.target.value);
    if(key==='pan')p.pan=val/100;else if(key==='padGain')p.gain=val/100;else if(key==='sampleStart')p.start=Math.min(val/100,p.end-.01);else if(key==='sampleEnd')p.end=Math.max(val/100,p.start+.01);else p[key]=val;
    updateLabels();drawWaveform();refreshScreens();persist();
  };
}
$('resetShape').onclick=()=>{Object.assign(state.pads[state.selected],{pitch:0,attack:4,release:250,filter:20000});selectPad(state.selected);persist();};
window.addEventListener('keydown',e=>{
  if(e.target.matches('input,textarea,select')||e.repeat||e.metaKey||e.ctrlKey||e.altKey)return;
  const key=e.key.toLowerCase(),idx=KEY_MAP.indexOf(key);
  if(idx>=0){e.preventDefault();triggerPad(idx);}
  if(e.code==='Space'){e.preventDefault();void togglePlay();}
  if(key==='escape')stop();
});
let dragCounter=0;
$('dropZone').addEventListener('dragenter',e=>{e.preventDefault();dragCounter++;$('dropZone').classList.add('drag-active');});
$('dropZone').addEventListener('dragover',e=>e.preventDefault());
$('dropZone').addEventListener('dragleave',e=>{e.preventDefault();if(--dragCounter<=0){dragCounter=0;$('dropZone').classList.remove('drag-active');}});
$('dropZone').addEventListener('drop',e=>{e.preventDefault();dragCounter=0;$('dropZone').classList.remove('drag-active');void loadFiles(e.dataTransfer.files,state.selected);});

$('midiBtn').onclick=async()=>{
  if(!navigator.requestMIDIAccess){status('Web MIDI unsupported here. Use Chrome or an enabled Chromium browser.');return;}
  try{
    const access=await navigator.requestMIDIAccess();let count=0;
    const attach=()=>{count=0;for(const input of access.inputs.values()){
      count++;input.onmidimessage=e=>{const [statusByte,note,velocity]=e.data;const type=statusByte&0xf0;if(type===0x90&&velocity>0){if(note>=36&&note<52)triggerPad(note-36,null,velocity/127);else playKeyboardNote(note);}else if(type===0xb0){const selected=state.pads[state.selected];selected.filter=clamp(250+velocity/127*19750,150,20000);selectPad(state.selected);}};
    }$('midiStatus').textContent=count?`MIDI: ${count} INPUT${count===1?'':'S'} CONNECTED`:'MIDI: NO INPUT DEVICES';};
    attach();access.onstatechange=attach;status(`Web MIDI enabled: ${count} device(s). Pads C2–D#3 / MIDI 36–51.`);
  }catch(err){status('MIDI permission denied or unavailable: '+err.message);}
};
$('audioExportBtn').onclick=async()=>{
  if(mediaRecorder?.state==='recording'){mediaRecorder.stop();$('audioExportBtn').textContent='RECORD AUDIO PERFORMANCE';return;}
  if(!await ensureAudio())return;
  if(!window.MediaRecorder){status('Browser audio recording unavailable.');return;}
  const supported=['audio/webm;codecs=opus','audio/mp4','audio/webm'].find(t=>MediaRecorder.isTypeSupported(t));
  mediaRecorder=new MediaRecorder(recorderDestination.stream,supported?{mimeType:supported}:{});recordedChunks=[];
  mediaRecorder.ondataavailable=e=>{if(e.data.size)recordedChunks.push(e.data);};
  mediaRecorder.onstop=()=>{if(recordedChunks.length)download(new Blob(recordedChunks,{type:mediaRecorder.mimeType}),`maschine-lab-performance.${mediaRecorder.mimeType.includes('mp4')?'m4a':'webm'}`);status('Audio performance recording exported.');};
  mediaRecorder.start();$('audioExportBtn').textContent='STOP & DOWNLOAD RECORDING';status('Recording master audio output; play pads or sequence, then stop recording.');
};

renderSequencer();selectPad(0);
status('Built-in kit ready. Enable audio, trigger the pads, or import your own WAV/MP3 samples.');
window.MaschineLab={state,triggerPad,togglePlay,stop,loadFiles,midiExport,fitCamera,setKeyboard};
