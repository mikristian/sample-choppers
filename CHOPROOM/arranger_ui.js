/* Isolated timeline editor. No changes to source chops or pads. */
(function(root){
'use strict';
const E=root.ChopArrangerEngine;
if(!E)throw Error('Load arranger_engine.js first');
let host=null,opt={},state=E.create(),ctx=null,nodes=[],playing=false,timer=0,startClock=0,startPosition=0,trayBank=0;
let renderScale=80,playEnd=0,playingClipId=null,playGeneration=0,resizeObserver=null,lastHostWidth=0,auditionGain=null;
let undoStack=[],redoStack=[];
const HISTORY_LIMIT=45;
const element=(tag,cls,txt)=>{const n=document.createElement(tag);if(cls)n.className=cls;if(txt!==undefined)n.textContent=txt;return n;};
const q=s=>host?.querySelector(s);
const audio=()=>{const a=opt.getAudio?.();return a?.audioBuffer||a?.buffer||a||null;};
const tempo=()=>Number(opt.getTempo?.())||90;
const range=ref=>{
 const m=opt.getMarkers?.()||[],v=opt.getVariants?.()||[];
 if(Number.isInteger(ref)&&ref>=0&&ref<m.length-1)return {start:m[ref],end:m[ref+1],label:'Chop '+(ref+1)};
 const item=v.find(x=>x.id===ref);return item?{start:item.start,end:item.end,label:item.label||ref}:null;
};
const snapshot=()=>JSON.parse(JSON.stringify(state));
const change=(next,record=true)=>{
 const updated=E.normalize(next);
 if(JSON.stringify(updated)===JSON.stringify(state))return false;
 if(playing)stop();
 if(record){
  undoStack.push(snapshot());
  if(undoStack.length>HISTORY_LIMIT)undoStack.shift();
  redoStack.length=0;
 }
 state=updated;draw();opt.onChange?.(snapshot());
 return true;
};
function undo(){
 if(!undoStack.length)return false;
 const previous=undoStack.pop();
 redoStack.push(snapshot());return change(previous,false);
}
function redo(){
 if(!redoStack.length)return false;
 const next=redoStack.pop();
 undoStack.push(snapshot());return change(next,false);
}
function restore(s){
 stop();state=E.normalize(s);undoStack=[];redoStack=[];draw();return snapshot();
}
function sourceChanged(){stop();draw();}
function autoPopulate(){
 const refs=opt.getChopRefs?.()||[];
 const duration=audio()?.duration;
 if(!refs.length||!Number.isFinite(duration))return 0;
 const sources=refs.map(ref=>{
  const r=range(ref);
  return r&&r.start<duration?{ref,start:r.start,end:Math.min(duration,r.end),label:r.label}:null;
 }).filter(Boolean);
 const before=state.clips.length;
 change(E.populateFromChops(state,sources,.48));
 return Math.max(0,state.clips.length-before);
}
function addChop(ref,options={}){
 const r=range(ref);if(!r||state.clips.length>=E.MAX_CLIPS)return false;
 const duration=audio()?.duration;
 if(Number.isFinite(duration)){r.end=Math.min(duration,r.end);if(r.end-r.start<E.MIN)return false;}
 change(E.addClip(state,ref,r,options));return true;
}
function stop(){
 playGeneration++;
 if(playing)state=E.setView(state,{playhead:Math.min(playEnd||E.duration(state),Math.max(startPosition,startPosition+(ctx?.currentTime||0)-startClock))});
 playing=false;cancelAnimationFrame(timer);timer=0;
 for(const n of nodes){try{n.stop();}catch{}try{n.disconnect();}catch{}}
 if(auditionGain){try{auditionGain.disconnect();}catch{}auditionGain=null;}
 nodes=[];playingClipId=null;
 const b=q('.ar-play');if(b)b.textContent='▶ Play';
 host?.removeAttribute('data-previewing-clip');
 for(const clip of host?.querySelectorAll('.ar-clip.ar-auditioning')||[])clip.classList.remove('ar-auditioning');
 q('.ar-source')?.classList.remove('ar-auditioning-source');
 const head=q('.ar-head');if(head)head.style.left=state.playhead*renderScale+'px';
 const clock=q('.ar-clock');if(clock)clock.textContent=state.playhead.toFixed(2)+'s';
}
function tickPlayback(generation){
 if(!playing||generation!==playGeneration)return;
 const t=startPosition+Math.max(0,(ctx?.currentTime||0)-startClock);
 const head=q('.ar-head');if(head)head.style.left=Math.min(playEnd,t)*renderScale+'px';
 const clock=q('.ar-clock');if(clock)clock.textContent=Math.min(playEnd,t).toFixed(2)+'s';
 if(t>=playEnd-.005){stop();draw();return;}
 timer=requestAnimationFrame(()=>tickPlayback(generation));
}
// A normal click auditions this exact clip, not the entire song or its pad
// bank. Clicks never change the source cut markers; drag/trim work separately.
async function auditionClip(id){
 const c=state.clips.find(item=>item.id===id);
 const buffer=audio(),Context=root.AudioContext||root.webkitAudioContext;
 if(!c||!Context||!buffer?.getChannelData)return false;
 opt.onBeforePlay?.();
 stop();
 const generation=playGeneration;
 change(E.selectClip(E.setView(state,{playhead:c.start}),id),false);
 const duration=Math.min(c.sourceEnd,buffer.duration)-c.sourceStart;
 if(duration<E.MIN)return false;
 ctx=ctx||new Context();
 if(ctx.state==='suspended')await ctx.resume();
 if(generation!==playGeneration)return false;
 const source=ctx.createBufferSource(),gain=ctx.createGain();
 source.buffer=buffer;
 source.connect(gain);gain.connect(ctx.destination);
 startPosition=c.start;startClock=ctx.currentTime+.025;playEnd=c.start+duration;
 const fade=Math.min(.008,duration/4);
 gain.gain.setValueAtTime(0,startClock);
 gain.gain.linearRampToValueAtTime(.86,startClock+fade);
 gain.gain.setValueAtTime(.86,startClock+Math.max(fade,duration-fade));
 gain.gain.linearRampToValueAtTime(0,startClock+duration);
 auditionGain=gain;
 playingClipId=id;playing=true;nodes=[source];
 source.start(startClock,c.sourceStart,duration);
 host?.setAttribute('data-previewing-clip',id);
 q('.ar-clip[aria-selected="true"]')?.classList.add('ar-auditioning');
 const button=q('.ar-play');if(button)button.textContent='■ Stop';
 tickPlayback(generation);
 return true;
}
async function auditionSourceAt(time){
 const buffer=audio(),Context=root.AudioContext||root.webkitAudioContext;
 if(!Context||!buffer?.getChannelData)return false;
 opt.onBeforePlay?.();
 const from=Math.max(0,Math.min(buffer.duration-.03,Number(time)||0));
 const duration=buffer.duration-from;
 stop();
 const generation=playGeneration;
 change(E.setView(state,{playhead:from}),false);
 ctx=ctx||new Context();
 if(ctx.state==='suspended')await ctx.resume();
 if(generation!==playGeneration)return false;
 const source=ctx.createBufferSource(),gain=ctx.createGain();
 source.buffer=buffer;gain.gain.value=.78;
 source.connect(gain);gain.connect(ctx.destination);
 startPosition=from;startClock=ctx.currentTime+.025;playEnd=buffer.duration;
 playing=true;playingClipId='source';auditionGain=gain;nodes=[source];
 source.start(startClock,from,duration);
 host?.setAttribute('data-previewing-clip','source');
 q('.ar-source')?.classList.add('ar-auditioning-source');
 const button=q('.ar-play');if(button)button.textContent='■ Stop';
 tickPlayback(generation);
 return true;
}
async function play(){
 if(playing){stop();draw();return true;}
 const buffer=audio(),Context=root.AudioContext||root.webkitAudioContext;
 if(!Context||!buffer?.getChannelData)return false;
 opt.onBeforePlay?.();
 if(E.duration(state)>0&&state.playhead>=E.duration(state)-.001)state=E.setView(state,{playhead:0});
 ctx=ctx||new Context();if(ctx.state==='suspended')await ctx.resume();
 startPosition=state.playhead;startClock=ctx.currentTime+.04;
 const solo=state.tracks.some(t=>t.solo);
 for(const c of state.clips){
  const t=state.tracks.find(t=>t.id===c.trackId);
  if(!t||t.mute||(solo&&!t.solo))continue;
  const offset=Math.max(0,startPosition-c.start);
  const from=c.sourceStart+offset;
  const length=Math.min(c.sourceEnd,buffer.duration)-from;
  if(c.start+c.sourceEnd-c.sourceStart<=startPosition||length<=0||from<0)continue;
   const n=ctx.createBufferSource();n.buffer=buffer;n.connect(ctx.destination);
   n.start(startClock+Math.max(0,c.start-startPosition),from,length);nodes.push(n);
 }
 if(!nodes.length)return false;
 playing=true;playingClipId=null;playEnd=E.duration(state);
 q('.ar-play').textContent='■ Stop';
 tickPlayback(playGeneration);return true;
}
function btn(txt,fn){const n=element('button','',txt);n.type='button';n.onclick=fn;return n;}
function exportJSON(){
 const content=JSON.stringify({app:'CHOPROOM',type:'arrangement',version:1,arrangement:snapshot()},null,2);
 const url=URL.createObjectURL(new Blob([content],{type:'application/json'}));
 const link=element('a');link.href=url;link.download='choproom-arrangement.json';
 document.body.append(link);link.click();link.remove();
 setTimeout(()=>URL.revokeObjectURL(url),1000);
}
async function exportWAV(){
 const button=q('.ar-render');
 const buffer=audio();
 if(!buffer?.getChannelData||!state.clips.length||!E.duration(state))return false;
 const duration=E.duration(state)+.10,rate=buffer.sampleRate;
 if(duration>600){if(button)button.textContent='Max 10 minutes';return false;}
 const Context=root.OfflineAudioContext||root.webkitOfflineAudioContext;
 if(!Context){if(button)button.textContent='Offline rendering unsupported';return false;}
 const saveLabel=button?.textContent||'Export mix WAV';
 if(button){button.disabled=true;button.textContent='Rendering mix…';}
 try{
  const offline=new Context(2,Math.ceil(duration*rate),rate);
  const anySolo=state.tracks.some(t=>t.solo);
  for(const clip of state.clips){
   const lane=state.tracks.find(t=>t.id===clip.trackId);
   if(!lane||lane.mute||(anySolo&&!lane.solo))continue;
   const from=clip.sourceStart;
   const length=Math.min(clip.sourceEnd,buffer.duration)-from;
   if(from<0||length<=0)continue;
   const source=offline.createBufferSource();source.buffer=buffer;
   const gain=offline.createGain();gain.gain.value=.88;
   source.connect(gain);gain.connect(offline.destination);
   source.start(clip.start,from,length);
  }
  const rendered=await offline.startRendering();
  if(button)button.textContent='Encoding 16-bit WAV…';
  const count=rendered.length;
  const data=new ArrayBuffer(44+count*4);
  const view=new DataView(data);
  const str=(at,text)=>{for(let i=0;i<text.length;i++)view.setUint8(at+i,text.charCodeAt(i));};
  str(0,'RIFF');view.setUint32(4,36+count*4,true);str(8,'WAVE');
  str(12,'fmt ');view.setUint32(16,16,true);view.setUint16(20,1,true);
  view.setUint16(22,2,true);view.setUint32(24,rate,true);
  view.setUint32(28,rate*4,true);view.setUint16(32,4,true);view.setUint16(34,16,true);
  str(36,'data');view.setUint32(40,count*4,true);
  const l=rendered.getChannelData(0),r=rendered.getChannelData(1);
  for(let i=0,at=44;i<count;i++,at+=4){
   const a=Math.max(-1,Math.min(1,l[i])),b=Math.max(-1,Math.min(1,r[i]));
   view.setInt16(at,Math.round(a<0?a*32768:a*32767),true);
   view.setInt16(at+2,Math.round(b<0?b*32768:b*32767),true);
  }
  const url=URL.createObjectURL(new Blob([data],{type:'audio/wav'}));
  const link=element('a');link.href=url;
  link.download='CHOPROOM_arrangement_'+Math.round(tempo())+'BPM.wav';
  document.body.append(link);link.click();link.remove();
  setTimeout(()=>URL.revokeObjectURL(url),60000);
  if(button)button.textContent='WAV downloaded';
  return true;
 }catch(err){
  if(button)button.textContent='Render failed: '+String(err?.message||err).slice(0,40);
  return false;
 }finally{
  if(button){button.disabled=false;
   // The result label is only visible until the next timeline edit/redraw.
   button.title='Export current arrangement as 16-bit stereo WAV, preserving gaps';
  }
 }
}
function readDroppedRef(ev){
 const custom=ev.dataTransfer?.getData('application/x-choproom-ref');
 const plain=ev.dataTransfer?.getData('text/plain');
 const fallback=root.__CHOPROOM_DRAG_REF?.ref;
 const data=custom||(plain?.startsWith('choproom-ref:')?plain.slice(13):null);
 if(data){
  const n=Number(data);
  return /^\d+$/.test(data)?n:data;
 }
 return Number.isInteger(fallback)||typeof fallback==='string'?fallback:null;
}
function dropOnLane(ev,trackId,lane){
 ev.preventDefault();
 const ref=readDroppedRef(ev);
 if(ref===null||!range(ref))return;
 ev.stopPropagation();
 root.__CHOPROOM_DRAG_REF=null;
 const position=Math.max(0,(ev.clientX-lane.getBoundingClientRect().left)/renderScale);
 const start=E.snapSeconds(position,state.snap,tempo());
 addChop(ref,{trackId,start:Math.max(0,start)});
}
function makeChopTray(){
 const banks=opt.getBanks?.()||[],bank=banks[Math.min(trayBank,banks.length-1)]||[];
 const tray=element('div','ar-tray');
 const head=element('div','ar-tray-head');
 const titles=element('div','ar-tray-copy');
 titles.append(element('strong','','CHOP SOURCES'),
  element('span','','Drag a loaded pad onto a track below · click to place at the playhead'));
 const tools=element('div','ar-tray-controls');
 const label=element('label','','SOURCE BANK');
 const select=element('select');
 select.setAttribute('aria-label','Choose source pad bank for the arrangement');
 for(let b=0;b<banks.length;b++){
  const name='Bank '+String.fromCharCode(65+(b%26))+(b>=26?' '+(Math.floor(b/26)+1):'');
  const o=element('option','',name);
  o.value=String(b);select.append(o);
 }
 select.value=String(Math.max(0,Math.min(trayBank,banks.length-1)));
 select.onchange=()=>{trayBank=Number(select.value);draw();};
 label.append(select);
 tools.append(label);
 head.append(titles,tools);tray.append(head);
 const scroller=element('div','ar-tray-scroll');
 const grid=element('div','ar-tray-grid');
 for(let i=0;i<16;i++){
  const ref=bank[i],r=range(ref),valid=ref!==null&&ref!==undefined&&r&&r.end>r.start;
  const item=btn('',()=>{if(valid)addChop(ref);});
  item.className='ar-tray-item';
  item.disabled=!valid;
  item.draggable=!!valid;
  item.dataset.pad=String(i);
  item.title=valid?'Place '+r.label+' at playhead; or drag onto a track':'Pad '+(i+1)+' is empty';
  item.setAttribute('aria-label',valid?
    'Source Pad '+(i+1)+' · '+r.label+' · click to place, drag to track':'Empty source pad '+(i+1));
  const no=element('span','ar-tray-num',String(i+1).padStart(2,'0'));
  const text=element('span','ar-tray-label',valid?r.label:'Empty');
  const time=element('span','ar-tray-duration',valid?(r.end-r.start).toFixed(2)+'s':'—');
  item.append(no,text,time);
  if(valid){
   item.addEventListener('dragstart',ev=>{
    root.__CHOPROOM_DRAG_REF={ref};
    ev.dataTransfer.effectAllowed='copy';
    ev.dataTransfer.setData('application/x-choproom-ref',String(ref));
    ev.dataTransfer.setData('text/plain','choproom-ref:'+String(ref));
    grid.classList.add('ar-tray-dragging');
   });
   item.addEventListener('dragend',()=>{
    root.__CHOPROOM_DRAG_REF=null;
    grid.classList.remove('ar-tray-dragging');
   });
  }
  grid.append(item);
 }
 scroller.append(grid);tray.append(scroller);
 return tray;
}
function wave(canvas,start,end,kind='clip'){
 const len=Math.max(.03,end-start),width=Math.max(1,Math.ceil(len*renderScale));
 canvas.width=Math.min(4096,width);canvas.height=50;
 const g=canvas.getContext('2d');if(!g)return;
 const buffer=audio(),peaks=opt.getPeaks?.(),duration=buffer?.duration||0;
 const samples=buffer?.getChannelData?.(0);
 const levels=new Float32Array(canvas.width);
 let maxLevel=.001;
 for(let x=0;x<canvas.width;x++){
  const t=start+(x+.5)/canvas.width*len;
  let level=.08;
  if(peaks?.length&&duration){
   const a=Math.max(0,Math.floor((start+x/canvas.width*len)/duration*peaks.length));
   const b=Math.min(peaks.length,Math.ceil((start+(x+1)/canvas.width*len)/duration*peaks.length));
   level=0;
   for(let i=a;i<Math.max(a+1,b);i++)level=Math.max(level,Number(peaks[Math.min(i,peaks.length-1)])||0);
  }else if(samples&&buffer?.sampleRate){
   const begin=Math.max(0,Math.floor(t*buffer.sampleRate));
   const stop=Math.min(samples.length,begin+Math.ceil(buffer.sampleRate*len/canvas.width));
   level=0;
   for(let i=begin;i<stop;i+=Math.max(1,Math.floor((stop-begin)/18)))level=Math.max(level,Math.abs(samples[i]));
  }
  levels[x]=level;maxLevel=Math.max(maxLevel,level);
 }
 // Display gain affects only the drawing, never audio playback or export.
 // Quiet vocals remain recognizable instead of rendering as tiny dots.
 const visibleGain=Math.max(.75,Math.min(3.6,.80/maxLevel));
 g.strokeStyle=kind==='source'?'rgba(144,152,166,.34)':'rgba(37,47,57,.17)';
 g.beginPath();g.moveTo(0,25.5);g.lineTo(canvas.width,25.5);g.stroke();
 g.fillStyle=kind==='source'
  ?(document.documentElement.dataset.theme==='light'?'#617894':'#a9b6c9')
  :'rgba(30,42,57,.68)';
 for(let x=0;x<canvas.width;x++){
  const h=Math.max(1,Math.min(23,levels[x]*visibleGain*22));
  g.fillRect(x,25-h,1,h*2);
 }
}
function drag(e,c,clip,scroll){
 if(e.button!==0)return;
 e.preventDefault();clip.setPointerCapture(e.pointerId);
 const initial=state,originX=e.clientX,originY=e.clientY;
 const side=e.target.classList.contains('ar-left')?'left':e.target.classList.contains('ar-right')?'right':null;
 let delta=0,trackId=c.trackId;
 const moving=ev=>{
  delta=(ev.clientX-originX)/renderScale;
  if(!side){
   clip.style.transform='translate('+(ev.clientX-originX)+'px,'+(ev.clientY-originY)+'px)';
   const row=Math.floor((ev.clientY-scroll.getBoundingClientRect().top+scroll.scrollTop-116)/86);
   trackId=state.tracks[Math.max(0,Math.min(row,state.tracks.length-1))]?.id||c.trackId;
  }else if(side==='left'){
   const d=Math.max(-Math.min(c.sourceStart,c.start),Math.min(c.sourceEnd-c.sourceStart-E.MIN,delta));
   clip.style.left=(c.start+d)*renderScale+'px';clip.style.width=Math.max(4,(c.sourceEnd-c.sourceStart-d)*renderScale)+'px';
  }else clip.style.width=Math.max(4,(c.sourceEnd-c.sourceStart+delta)*renderScale)+'px';
 };
 const clean=()=>{clip.removeEventListener('pointermove',moving);clip.removeEventListener('pointerup',done);clip.removeEventListener('pointercancel',cancel);};
 const cancel=()=>{clean();draw();};
 const done=ev=>{
  clean();
  if(Math.abs(ev.clientX-originX)<4&&Math.abs(ev.clientY-originY)<4){
   if(!side)auditionClip(c.id);
   else change(E.selectClip(initial,c.id),false);
   return;
  }
  change(side?E.trimClip(initial,c.id,side,delta,tempo(),audio()?.duration):E.moveClip(initial,c.id,c.start+delta,trackId,tempo()));
 };
 clip.addEventListener('pointermove',moving);clip.addEventListener('pointerup',done);clip.addEventListener('pointercancel',cancel);
}
function draw(){
 if(!host)return;
 const oldScroll=q('.ar-scroll'),oldX=oldScroll?.scrollLeft||0,oldY=oldScroll?.scrollTop||0;
 host.replaceChildren();
 const bar=element('div','ar-toolbar');bar.append(element('strong','','ARRANGEMENT'));
 const playButton=btn('▶ Play',()=>play());playButton.className='ar-play';bar.append(playButton);
 const picker=element('select','ar-clip-picker');
 picker.setAttribute('aria-label','Choose a clip to play from the arrangement');
 picker.title='Audition any arranged chop, including short clips in the full-song view';
 const placeholder=element('option','','▶ Choose a chop to hear');placeholder.value='';
 picker.append(placeholder);
 for(const c of [...state.clips].sort((a,b)=>a.start-b.start)){
  const option=element('option','',c.label+' · '+c.start.toFixed(1)+'s');
  option.value=c.id;picker.append(option);
 }
 picker.value=state.selected||'';
 picker.onchange=()=>{if(picker.value)auditionClip(picker.value);};
 bar.append(picker);
 const undoButton=btn('↶ Undo',undo);undoButton.className='ar-undo';undoButton.disabled=!undoStack.length;undoButton.title='Undo the last arrangement edit';bar.append(undoButton);
 const redoButton=btn('↷ Redo',redo);redoButton.className='ar-redo';redoButton.disabled=!redoStack.length;redoButton.title='Redo the last arrangement edit';bar.append(redoButton);
 const place=btn('+ Place selected chop',()=>{const selected=opt.getSelectedChop?.();if(selected!==null&&selected!==undefined)addChop(selected);});
 place.disabled=typeof opt.getSelectedChop!=='function';bar.append(place);
 bar.append(btn('+ Track',()=>change(E.addTrack(state))));
 const rebuild=btn('Rebuild auto row',()=>{
  if(!root.confirm('Rebuild only the rows with AUTO enabled? Their current clips and positions will be replaced. Manual rows and source chops stay unchanged.'))return;
  change(E.resetAutoRows(state));autoPopulate();
 });
 rebuild.title='Only affects AUTO rows. Requires confirmation and can be undone.';
 bar.append(rebuild);
 const render=btn('Export mix WAV',()=>exportWAV());
 render.className='ar-render';render.disabled=!state.clips.length;
 render.title='Render the assembled timeline (including blank gaps) as a stereo WAV file';
 bar.append(render);
 bar.append(btn('Export Arrangement JSON',exportJSON));
 const label=element('label','','Snap '),sel=element('select');
 for(const name of ['off','beat','bar']){const o=element('option','',name);o.value=name;sel.append(o);}
 sel.value=state.snap;sel.onchange=()=>change(E.setView(state,{snap:sel.value}));label.append(sel);bar.append(label);
 const fit=btn('⤢ Fit full song',()=>change(E.setView(state,{view:'fit'})));
 fit.className='ar-fit';fit.title='Show the entire source song and arranged clips at once, without horizontal scrolling';
 fit.setAttribute('aria-pressed',String(state.view==='fit'));bar.append(fit);
 const zoomOut=btn('−',()=>change(E.setView(state,{zoom:state.view==='fit'?Math.max(20,renderScale/1.25):state.zoom/1.25,view:'detail'})));
 zoomOut.className='ar-zoom-out';bar.append(zoomOut);
 const zoomLabel=element('span','ar-zoom-label',state.view==='fit'?'FULL SONG':Math.round(state.zoom)+' px/s');bar.append(zoomLabel);
 const zoomIn=btn('+',()=>change(E.setView(state,{zoom:state.view==='fit'?Math.max(20,renderScale*1.8):state.zoom*1.25,view:'detail'})));
 zoomIn.className='ar-zoom-in';bar.append(zoomIn);
 host.append(bar);
 host.append(makeChopTray());
 const main=element('div','ar-main'),names=element('div','ar-names'),scroll=element('div','ar-scroll');
 main.append(names,scroll);host.append(main);
 const total=Math.max(20,E.duration(state)+Math.min(2,Math.max(1,E.duration(state)*.015)),audio()?.duration||0);
 // Measure the actual lane width after the grid has been laid out. Unlike
 // 80px/second, fit mode displays a 4-minute song without any side swipe.
 const laneWidth=Math.max(100,scroll.clientWidth);
 renderScale=state.view==='fit'?(laneWidth-2)/total:state.zoom;
 const width=state.view==='fit'?laneWidth:Math.ceil(total*renderScale);
 scroll.classList.toggle('ar-fit-scroll',state.view==='fit');
 const ruler=element('div','ar-ruler');ruler.style.width=width+'px';
 const tickEvery=state.view==='fit'
  ?Math.max(1,Math.ceil(total/Math.max(2,Math.floor(laneWidth/75)) / 5)*5)
  :renderScale>=80?1:renderScale>=40?2:5;
 for(let t=0;t<total;t+=tickEvery){
  const tick=element('span','ar-tick',t.toFixed(0)+'s');tick.style.left=t*renderScale+'px';ruler.append(tick);
 }
 ruler.onclick=e=>{stop();change(E.setView(state,{playhead:(e.clientX-ruler.getBoundingClientRect().left)/renderScale}),false);};
 ruler.ondblclick=()=>{play();};
 scroll.append(ruler);
 const sourceName=element('div','ar-name ar-name-source','SOURCE · READ ONLY');names.append(sourceName);
 const area=element('div','ar-area');area.style.width=width+'px';
  const source=element('div','ar-lane ar-source');const sourceCanvas=element('canvas','ar-wave');
  if(playing&&playingClipId==='source')source.classList.add('ar-auditioning-source');
 source.title='Click the original source track to listen from that point without changing your chops';
 source.onclick=e=>{
  if(!audio()?.duration)return;
  const position=(e.clientX-source.getBoundingClientRect().left)/renderScale;
  auditionSourceAt(position);
 };
 if(audio()?.duration){wave(sourceCanvas,0,audio().duration,'source');sourceCanvas.style.width=(audio().duration*renderScale)+'px';source.append(sourceCanvas);}
 area.append(source);
 const colors=['#a4aec2','#bb9dbc','#90adb2','#c3ac86','#a3b7a0','#c3a19c','#a9a7c2','#9fbdc4'];
 for(const [trackIndex,track] of state.tracks.entries()){
  const name=element('div','ar-name');const input=element('input');input.value=track.name;input.setAttribute('aria-label','Track name');
  name.style.setProperty('--lane-color',colors[trackIndex%colors.length]);
  input.onchange=()=>change(E.setTrack(state,track.id,{name:input.value}));name.append(input);
  for(const key of ['mute','solo']){const b=btn(key==='mute'?'M':'S',()=>change(E.setTrack(state,track.id,{[key]:!track[key]})));b.setAttribute('aria-pressed',String(track[key]));b.title=key;name.append(b);}
  const auto=btn('AUTO',()=>{
   const enabled=!track.auto;
   change(E.setTrack(state,track.id,{auto:enabled}));
   if(enabled)autoPopulate();
  });
  auto.className='ar-auto';
  auto.title=track.auto?'This lane receives new chops automatically. Click to disable.':'Manual lane. Click to start auto-placing chops on this lane.';
  auto.setAttribute('aria-label',track.auto?'Disable automatic chops for '+track.name:'Enable automatic chops for '+track.name);
  auto.setAttribute('aria-pressed',String(!!track.auto));
  name.append(auto);
  names.append(name);
  const lane=element('div','ar-lane');lane.style.backgroundSize=(renderScale*4)+'px 100%';
  lane.style.setProperty('--lane-color',colors[trackIndex%colors.length]);
  lane.dataset.trackId=track.id;
  lane.addEventListener('dragover',ev=>{
   const types=Array.from(ev.dataTransfer?.types||[]);
   if(root.__CHOPROOM_DRAG_REF||
     types.includes('application/x-choproom-ref')||
     types.includes('text/plain')){
    ev.preventDefault();
    if(ev.dataTransfer)ev.dataTransfer.dropEffect='copy';
    lane.classList.add('ar-drop-target');
   }
  });
  lane.addEventListener('dragleave',()=>lane.classList.remove('ar-drop-target'));
  lane.addEventListener('drop',ev=>dropOnLane(ev,track.id,lane));
  lane.addEventListener('click',ev=>{
   if(ev.target.closest('.ar-clip'))return;
   const position=Math.max(0,(ev.clientX-lane.getBoundingClientRect().left)/renderScale);
   stop();
   change(E.setView(state,{playhead:position}),false);
   // A blank section previews the arranged mix from that time. Source
   // and chop pads remain unchanged, so gaps remain actual silence.
   play();
  });
  if(track.mute||(state.tracks.some(t=>t.solo)&&!track.solo))lane.style.opacity='.45';
  for(const c of state.clips.filter(c=>c.trackId===track.id)){
   const clip=element('div','ar-clip');
   const scaledLength=(c.sourceEnd-c.sourceStart)*renderScale;
   clip.style.left=c.start*renderScale+'px';
   clip.style.width=Math.max(5,scaledLength)+'px';
   clip.classList.toggle('ar-compact-clip',scaledLength<33);
   clip.style.setProperty('--clip-color',colors[trackIndex%colors.length]);
   if(playing&&playingClipId===c.id)clip.classList.add('ar-auditioning');
   clip.tabIndex=0;clip.setAttribute('role','button');
   clip.setAttribute('aria-label','Clip '+c.label+'; click to play, drag to move, arrow keys move, Delete removes');
   clip.title=c.label+' · '+c.sourceStart.toFixed(2)+'s–'+c.sourceEnd.toFixed(2)+'s · Click to play, drag to rearrange';
   clip.setAttribute('aria-selected',String(state.selected===c.id));
   clip.append(element('span','ar-title',c.label));const cv=element('canvas','ar-wave');wave(cv,c.sourceStart,c.sourceEnd,'clip');clip.append(cv);
   clip.append(element('span','ar-handle ar-left'),element('span','ar-handle ar-right'));
   clip.onpointerdown=e=>drag(e,c,clip,scroll);
   clip.onkeydown=e=>{
    if(['Enter',' '].includes(e.key)){e.preventDefault();auditionClip(c.id);}
    else if(['Delete','Backspace'].includes(e.key)){e.preventDefault();change(E.removeClip(state,c.id));}
    else if(['ArrowLeft','ArrowRight'].includes(e.key)){e.preventDefault();change(E.moveClip(state,c.id,c.start+(e.key==='ArrowLeft'?-1:1)*60/tempo(),c.trackId,tempo()));}
   };
   lane.append(clip);
  }
  area.append(lane);
 }
 const head=element('div','ar-head');head.style.left=state.playhead*renderScale+'px';area.append(head);scroll.append(area);
 scroll.scrollLeft=state.view==='fit'?0:oldX;scroll.scrollTop=oldY;
 const foot=element('div','ar-footer');foot.append(element('span','ar-clock',state.playhead.toFixed(2)+'s'),element('span','',state.clips.length+' clips'));
 foot.append(element('span','ar-action-hint','Click a clip to play · Drag to move · Click source to listen'));
 const selected=state.clips.find(c=>c.id===state.selected);
 for(const [label,fn] of [
  ['Split at playhead',()=>change(E.splitClip(state,selected.id,state.playhead))],
  ['Duplicate',()=>change(E.duplicateClip(state,selected.id,undefined,undefined,tempo()))],
  ['Delete clip',()=>change(E.removeClip(state,selected.id))],
  ['Clear selection',()=>change(E.selectClip(state,null))]
 ]){
  const b=btn(label,fn);b.disabled=!selected;foot.append(b);
 }
 const split=foot.querySelector('button');if(split&&selected)split.disabled=state.playhead<=selected.start+E.MIN||state.playhead>=selected.start+selected.sourceEnd-selected.sourceStart-E.MIN;
 host.append(foot);if(playing)q('.ar-play').textContent='■ Stop';
}
function mount(config={}){
 opt=config;host=document.getElementById('arrangerWorkspace');if(!host)return false;
 trayBank=Number.isInteger(opt.getActiveBank?.())?Math.max(0,opt.getActiveBank()):0;
 if(!document.getElementById('chop-arranger-style')){
  const style=element('style');style.id='chop-arranger-style';style.textContent=[
   '.ar-root{font:13px system-ui,sans-serif;color:var(--ar-ink,#e7eeed);background:var(--ar-bg,#151c24);border:1px solid #3b484d;border-radius:12px;overflow:hidden;min-width:0}',
   '.ar-root button,.ar-root select{font:inherit;background:#283139;color:inherit;border:1px solid #526068;border-radius:5px;padding:6px;cursor:pointer}',
   '.ar-root button:focus-visible,.ar-root input:focus-visible,.ar-clip:focus-visible{outline:2px solid #9cdbae}',
   '.ar-root button[aria-pressed=true]{background:#a6deb1;color:#14221b}.ar-root button:disabled{opacity:.4}',
   '.ar-toolbar,.ar-footer{display:flex;align-items:center;gap:8px;flex-wrap:wrap;padding:10px;background:#202a32}',
   '.ar-toolbar strong{margin-right:auto}.ar-main{display:grid;grid-template-columns:132px minmax(0,1fr)}',
   '.ar-names{background:#202a32;border-right:1px solid #44535a}.ar-scroll{overflow:auto;max-height:590px;min-width:0}',
   '.ar-name{box-sizing:border-box;height:86px;padding:10px 6px;border-bottom:1px solid #44535a;font-weight:600;display:flex;gap:5px;flex-wrap:wrap;align-content:center}',
   '.ar-name input{background:transparent;border:0;color:inherit;width:100%;min-width:0;font:inherit}.ar-name button{font-size:11px;padding:4px 8px}',
   '.ar-ruler{position:relative;height:30px;background:#27323d;cursor:pointer}.ar-tick{position:absolute;top:0;bottom:0;border-left:1px solid #5c6970;font-size:10px;padding:4px;color:#b6c3cb;pointer-events:none}',
   '.ar-area{position:relative}.ar-lane{box-sizing:border-box;position:relative;height:86px;border-bottom:1px solid #44535a;background-image:linear-gradient(to right,#ffffff12 1px,transparent 1px)}',
   '.ar-source{background:#283e3a}.ar-clip{box-sizing:border-box;position:absolute;top:9px;height:66px;background:#336559;border:1px solid #9bd9c5;border-radius:5px;overflow:hidden;cursor:grab;touch-action:none;user-select:none}',
   '.ar-clip[aria-selected=true]{outline:2px solid #d2e99b;z-index:3}.ar-title{position:relative;z-index:1;display:block;margin:5px 12px;white-space:nowrap;font-size:11px;overflow:hidden;text-overflow:ellipsis;pointer-events:none}',
   '.ar-wave{display:block;pointer-events:none}.ar-clip .ar-wave{position:absolute;top:17px;left:0;height:46px;width:100%}.ar-source .ar-wave{height:70px;opacity:.7}',
   '.ar-handle{position:absolute;top:0;bottom:0;width:10px;background:#b0d7b13b;z-index:3;cursor:ew-resize;touch-action:none}.ar-left{left:0;border-left:2px solid #b2eeae}.ar-right{right:0;border-right:2px solid #b2eeae}',
   '.ar-head{position:absolute;top:0;bottom:0;border-left:2px solid #edb975;pointer-events:none;z-index:5}.ar-footer{border-top:1px solid #44535a}',
   '@media(max-width:600px){.ar-main{grid-template-columns:105px minmax(0,1fr)}.ar-name button{padding:3px 5px}}'
  ].join('');document.head.append(style);
 }
 host.classList.add('ar-root');draw();
 if(typeof ResizeObserver!=='undefined'&&!resizeObserver){
  resizeObserver=new ResizeObserver(entries=>{
   const width=entries[0]?.contentRect?.width||0;
   if(width&&Math.abs(width-lastHostWidth)>1){
    lastHostWidth=width;
    // Do not interrupt Web Audio while simply fitting the arrangement.
    requestAnimationFrame(()=>draw());
   }
  });
  resizeObserver.observe(host);
 }
 return true;
}
root.ChopArrangerUI={mount,restore,snapshot,sourceChanged,autoPopulate,undo,redo,addChop,play,stop,auditionClip,auditionSourceAt,exportJSON,exportWAV,splitSelected:()=>{const selected=state.selected;if(!selected)return false;const next=E.splitClip(state,selected,state.playhead);if(next.clips.length===state.clips.length)return false;change(next);return true;},addTrack:()=>change(E.addTrack(state))};
})(typeof window!=='undefined'?window:globalThis);
