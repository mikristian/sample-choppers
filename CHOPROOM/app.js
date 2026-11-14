(() => {
  'use strict';
  const $ = (id) => document.getElementById(id);
  const KEYS = ['1','2','3','4','q','w','e','r','a','s','d','f','z','x','c','v'];
  const PALETTE = ['#8da6bb','#b39cbd','#c8ab87','#8daab3','#b6a1aa','#adacbf','#bdafa0','#91a5c0',
                   '#c4a4ba','#a6afc4','#b7aa89','#99b0b5','#a89fbf','#b4a4aa','#94a3bd','#c8b3a0'];
  const PALETTE_LIGHT = ['#607f9b','#947b9e','#a27d56','#638c9e','#9c758c','#7a7d9b','#9a7f6d','#68819e',
                        '#9c7195','#7185a0','#998764','#6b8e90','#8c78aa','#987c87','#727e9d','#9c846f'];
  const isLight=()=>document.documentElement.dataset.theme==='light';
  const state = {
    audio:null, fileName:'', fileSize:0, duration:0, ctx:null, current:null, playTime:0,
    playbackStarted:0, playing:false, playMode:'', playingPad:-1, raf:0,
    markers:[], regionStart:0, regionEnd:0, padMap:Array.from({length:16},(_,i)=>i), settings:{},
    banks:[Array.from({length:16},(_,i)=>i===0?0:null)], activeBank:0, targetBank:0, targetPad:0, removed:[], variants:[], editingChop:0,
    selected:0, mode:'manual', gridCount:16, zoom:1, viewStart:0, peaks:null,
    transientCandidates:null, transientRegionKey:'', tool:'seek',
    dragged:-1, pointer:null, miniDrag:null, padDrag:null, padPick:null, mono:true,
    midi:null, midiInputs:[], midiMap:{}, midiLearning:-1,
    toastTimeout:0,
  };
  try{state.midiMap=JSON.parse(localStorage.getItem('choproom-midi-v1')||'{}');}catch{state.midiMap={};}
  const padContainer=$('pads');
  const wave=$('waveform'),overview=$('overview');
  const mainCtx=wave.getContext('2d'),overviewCtx=overview.getContext('2d');
  const durationString=(t,ms=true)=>{
    t=Math.max(0,Number(t)||0);
    const min=Math.floor(t/60).toString().padStart(2,'0');
    const sec=Math.floor(t%60).toString().padStart(2,'0');
    return min+':'+sec+(ms?'.'+Math.floor(t%1*100).toString().padStart(2,'0'):'');
  };
  function sessionData(){
    return {app:'CHOPROOM',version:1,sourceFile:state.fileName,duration:state.duration,
      regionStart:state.regionStart,regionEnd:state.regionEnd,markers:state.markers,
      padMap:state.padMap,banks:state.banks,activeBank:state.activeBank,removed:state.removed,
      variants:state.variants,arrangement:window.ChopArrangerUI?.snapshot?.()||null,
      settings:state.settings,tempo:Number($('tempo').value),
      mode:state.mode,sensitivity:Number($('sensitivity').value),
      manualCutPercent:Number($('manualCutPercent').value)};
  }
  function localSessionKey(){
    return 'choproom-autosave-v1:'+state.fileName+':'+Math.round(state.duration*1000)+':'+state.fileSize;
  }
  function persistSession(){
    if(!available())return;
    try{localStorage.setItem(localSessionKey(),JSON.stringify(sessionData()));}catch{}
  }
  function restoreAutosave(){
    try{
      const data=JSON.parse(localStorage.getItem(localSessionKey())||'null');
      if(!data||data.app!=='CHOPROOM'||data.sourceFile!==state.fileName||Math.abs(data.duration-state.duration)>.1)return false;
      const marks=data.markers;
      if(!Array.isArray(marks)||marks.length<2||marks.length>513
        ||marks.some((t,i)=>!Number.isFinite(t)||t<0||t>state.duration||(i>0&&t-marks[i-1]<.03)))return false;
      state.markers=marks.slice();
      state.regionStart=marks[0];state.regionEnd=marks[marks.length-1];
      state.settings=data.settings&&typeof data.settings==='object'?data.settings:{};
      restoreCollection(data);
      window.ChopArrangerUI?.restore?.(data.arrangement);
      if(Number.isFinite(Number(data.sensitivity)))$('sensitivity').value=Math.max(0,Math.min(100,Number(data.sensitivity)));
      $('sensitivityText').textContent=$('sensitivity').value+'%';
      if(Number.isFinite(Number(data.manualCutPercent)))$('manualCutPercent').value=Math.max(1,Math.min(99,Number(data.manualCutPercent)));
      $('manualCutText').textContent=$('manualCutPercent').value+'%';
      if(['manual','grid','transient'].includes(data.mode))state.mode=data.mode;
      return true;
    }catch{return false;}
  }
  function toast(msg,error=false){
    const el=$('toast');el.textContent=msg;el.classList.toggle('error',error);
    el.classList.add('visible');clearTimeout(state.toastTimeout);
    state.toastTimeout=setTimeout(()=>el.classList.remove('visible'),4300);
  }
  function audioContext(){
    if(!state.ctx)state.ctx=new(window.AudioContext||window.webkitAudioContext)();
    if(state.ctx.state==='suspended')state.ctx.resume().catch(()=>{});
    return state.ctx;
  }
  function available(){return Boolean(state.audio&&state.duration>0);}
  function restoreCollection(data){
    const count=Math.max(0,state.markers.length-1);
    state.variants=ChopVariants.normalize(data.variants,state.markers,state.duration);
    const variantIds=state.variants.map(v=>v.id);
    if(Array.isArray(data.banks)&&data.banks.length){
      state.banks=ChopCollection.normalize(data.banks,count,variantIds);
    }else if(Array.isArray(data.padMap)&&data.padMap.length===16){
      state.banks=ChopCollection.normalize([data.padMap],count,variantIds);
    }else{
      state.banks=[ChopCollection.emptyBank()];
      state.banks[0][0]=count?0:null;
    }
    state.removed=Array.isArray(data.removed)?[...new Set(data.removed.filter(i=>
      (Number.isInteger(i)&&i>=0&&i<count)||(typeof i==='string'&&variantIds.includes(i))))]:[];
    for(const bank of state.banks)for(let p=0;p<16;p++)if(state.removed.includes(bank[p]))bank[p]=null;
    state.activeBank=Math.max(0,Math.min(state.banks.length-1,Number.isInteger(data.activeBank)?data.activeBank:0));
    state.padMap=state.banks[state.activeBank];
    state.editingChop=state.padMap.find(i=>activeSlice(i))??null;
    state.selected=Math.max(0,state.padMap.indexOf(state.editingChop));
    state.targetBank=state.activeBank;state.targetPad=state.selected;
  }
  function selectedSlice(){return state.editingChop;}
  function chopRange(i){return ChopVariants.getRange(i,state.markers,state.variants);}
  function activeSlice(i){return chopRange(i)!=null&&!state.removed.includes(i);}
  function chopLabel(i){return chopRange(i)?.label||'No chop';}
  function allChopRefs(){
    return [...Array.from({length:Math.max(0,state.markers.length-1)},(_,i)=>i),
      ...state.variants.map(v=>v.id)];
  }
  function chooseChop(i){
    if(!chopRange(i))return;
    state.editingChop=i;
    const p=state.padMap.indexOf(i);
    if(p>=0)state.selected=p;
    updateSelection();renderPads();renderVault();drawWave();
  }
  function redrawCollection(){
    state.padMap=state.banks[state.activeBank];
    updateRegionText();updateSelection();renderBanks();renderPads();renderVault();drawWave();persistSession();
    window.ChopArrangerUI?.sourceChanged?.();
    if(!state.pauseAutoSync)window.ChopArrangerUI?.autoPopulate?.();
  }
  function enabledControls(){
    for(const id of ['playSource','playSelected','addAtPlayhead','deleteCut','exportAll','exportBottom','saveProject','sliceNow','startAt','endAt']){
      $(id).disabled=!available();
    }
    syncToolUI();
  }
  function stopAudio(){
    if(state.current){
      const {source}=state.current;
      try{source.onended=null;source.stop(0);}catch{}
      try{source.disconnect();}catch{}
      try{state.current.gain.disconnect();}catch{}
    }
    state.current=null;state.playing=false;state.playingPad=-1;
    $('transportIcon').textContent='▶';
    document.querySelectorAll('.pad.active').forEach(el=>el.classList.remove('active'));
    if(state.raf)cancelAnimationFrame(state.raf);
    state.raf=0;drawWave();
  }
  function getPitch(index){return (state.settings[index]||{}).pitch||0;}
  function makeSource(start,end,index=null){
    const ctx=audioContext();
    const setting=index==null?{}:state.settings[index]||{};
    const source=ctx.createBufferSource();
    let buffer=state.audio;
    if(setting.reverse){
      const len=Math.max(1,Math.floor((end-start)*buffer.sampleRate));
      const out=ctx.createBuffer(buffer.numberOfChannels,len,buffer.sampleRate);
      const at=Math.floor(start*buffer.sampleRate);
      for(let c=0;c<buffer.numberOfChannels;c++){
        const from=buffer.getChannelData(c),to=out.getChannelData(c);
        for(let i=0;i<len;i++)to[i]=from[Math.min(from.length-1,at+len-i-1)]||0;
      }
      buffer=out;start=0;end=len/buffer.sampleRate;
    }
    source.buffer=buffer;
    const speed=Math.pow(2,(setting.pitch||0)/12);
    source.playbackRate.value=speed;
    const gain=ctx.createGain();gain.gain.value=.88;
    source.connect(gain);gain.connect(ctx.destination);
    const fade=Math.min((setting.fadeMs==null?8:setting.fadeMs)/1000,(end-start)/4);
    const realLength=(end-start)/speed;
    const currentTime=ctx.currentTime;
    const effectiveFade=fade/speed;
    gain.gain.setValueAtTime(0,currentTime);
    gain.gain.linearRampToValueAtTime(.88,currentTime+Math.max(.002,effectiveFade));
    gain.gain.setValueAtTime(.88,currentTime+Math.max(.002,realLength-effectiveFade));
    gain.gain.linearRampToValueAtTime(0,currentTime+realLength);
    return {source,gain,start,end,speed,realLength};
  }
  function beginAudio(start,end,index=null,padIndex=-1){
    if(!available())return;
    // Keep the primary sampler and multitrack arranger from accidentally
    // overlapping audio. Within the arranger, separately layered tracks
    // still play together when its transport starts.
    window.ChopArrangerUI?.stop?.();
    if(state.mono||padIndex<0)stopAudio();
    // The pads intentionally use mono-choke by default, like a classic MPC.
    const ctx=audioContext();
    const entry=makeSource(start,end,index);
    const token={...entry,id:Math.random(),padIndex,started:ctx.currentTime,
      origin:start,sourceEnd:end,reversed:index!=null&&Boolean(state.settings[index]?.reverse)};
    state.current=token;state.playing=true;state.playMode=padIndex>=0?'pad':'source';state.playingPad=padIndex;
    state.playTime=start;state.playbackStarted=ctx.currentTime;
    entry.source.onended=()=>{
      if(state.current!==token)return;
      state.current=null;state.playing=false;state.playingPad=-1;
      $('transportIcon').textContent='▶';
      document.querySelectorAll('.pad.active').forEach(el=>el.classList.remove('active'));
      drawWave();
    };
    try{entry.source.start(ctx.currentTime,entry.start,entry.end-entry.start);}catch(err){toast('Audio could not be started: '+err.message,true);return;}
    $('transportIcon').textContent='Ⅱ';
    padContainer.querySelectorAll('.pad').forEach((pad,i)=>pad.classList.toggle('active',i===padIndex));
    tick();
  }
  function tick(){
    if(!state.current||!state.playing)return;
    const now=state.ctx.currentTime;
    const elapsed=Math.max(0,(now-state.playbackStarted)*state.current.speed);
    state.playTime=state.current.reversed
      ?Math.max(state.current.origin,state.current.sourceEnd-elapsed)
      :Math.min(state.current.sourceEnd,state.current.origin+elapsed);
    $('transportTime').innerHTML=durationString(state.playTime)+' <span>/ '+durationString(state.duration)+'</span>';
    drawWave();
    state.raf=requestAnimationFrame(tick);
  }
  function playSource(){
    if(!available())return;
    if(state.playing&&state.playMode==='source'){stopAudio();return;}
    beginAudio(Math.max(0,Math.min(state.duration-.01,state.playTime)),state.duration);
  }
  function seekTo(time){
    if(!available())return;
    const target=Math.max(0,Math.min(state.duration-.01,time));
    if(state.playing){
      // Starting a new AudioBufferSourceNode actually moves playback.
      // Changing only the playhead would leave the sound at the old position.
      beginAudio(target,state.duration);
    }else{
      state.playTime=target;
      $('transportTime').innerHTML=durationString(target)+' <span>/ '+durationString(state.duration)+'</span>';
      drawWave();
    }
  }
  function getSliceIndex(padIndex){return state.padMap[padIndex];}
  function selectPad(padIndex){
    state.selected=Math.max(0,Math.min(15,padIndex));
    state.targetBank=state.activeBank;state.targetPad=state.selected;
    const idx=getSliceIndex(state.selected);
    state.editingChop=activeSlice(idx)?idx:null;
    updateSelection();renderPads();renderVault();drawWave();
    window.ChopDevices?.selectPad?.(state.selected);
  }
  function playPad(padIndex){
    if(!available())return;
    const idx=getSliceIndex(padIndex);
    if(!activeSlice(idx)){selectPad(padIndex);return;}
    selectPad(padIndex);
    focusChop(idx);
    const range=chopRange(idx);
    beginAudio(range.start,range.end,idx,padIndex);
  }
  function playSelected(){
    const i=selectedSlice();
    if(activeSlice(i))playChop(i);
  }
  function playChop(i){
    if(!activeSlice(i))return;
    chooseChop(i);
    focusChop(i);
    const range=chopRange(i);
    beginAudio(range.start,range.end,i);
  }
  function resetChopSettings(){
    if(!activeSlice(selectedSlice()))return;
    state.settings[selectedSlice()]={pitch:0,fadeMs:8,reverse:false};
    updateSelection();persistSession();toast('Pad settings reset.');
  }
  function resizeCanvas(c){
    const bounds=c.getBoundingClientRect(),dpr=Math.min(2,window.devicePixelRatio||1);
    const w=Math.max(1,Math.round(bounds.width*dpr)),h=Math.max(1,Math.round(bounds.height*dpr));
    if(c.width!==w||c.height!==h){c.width=w;c.height=h;}
    const g=c.getContext('2d');g.setTransform(dpr,0,0,dpr,0,0);
    return {g,w:bounds.width,h:bounds.height};
  }
  function computePeaks(buffer){
    const count=16384,mono=buffer.getChannelData(0),other=buffer.numberOfChannels>1?buffer.getChannelData(1):mono;
    const peaks=new Float32Array(count);
    for(let x=0;x<count;x++){
      const first=Math.floor(x/count*buffer.length),last=Math.max(first+1,Math.floor((x+1)/count*buffer.length));
      let top=0;const step=Math.max(1,Math.floor((last-first)/5));
      for(let i=first;i<last;i+=step){const value=Math.max(Math.abs(mono[i]||0),Math.abs(other[i]||0));if(value>top)top=value;}
      peaks[x]=top;
    }
    return peaks;
  }
  function visibleSpan(){return state.duration/Math.max(1,state.zoom);}
  function clampView(){
    state.viewStart=Math.max(0,Math.min(state.viewStart,Math.max(0,state.duration-visibleSpan())));
  }
  function focusChop(index,scrollToTrack=false){
    if(!available()||!activeSlice(index))return;
    const range=chopRange(index);
    const a=range.start,b=range.end,middle=(a+b)/2;
    if($('followPad').checked||scrollToTrack){
      const span=Math.max(1.5,Math.min(state.duration,(b-a)*1.75));
      state.zoom=Math.max(1,Math.min(128,state.duration/span));
      state.viewStart=middle-visibleSpan()/2;
      clampView();
      $('zoomAmount').textContent=state.zoom.toFixed(1)+'×';
    }
    drawWave();
    if(scrollToTrack)$('waveArea').scrollIntoView({behavior:'smooth',block:'center'});
  }
  function viewToX(t,w){return (t-state.viewStart)/visibleSpan()*w;}
  function xToView(x,w){return state.viewStart+Math.max(0,Math.min(1,x/w))*visibleSpan();}
  function chopAtTime(t){
    if(state.markers.length<2)return 0;
    let pos=0;
    while(pos<state.markers.length-2&&state.markers[pos+1]<=t)pos++;
    return pos;
  }
  function drawWave(){
    const {g,w,h}=resizeCanvas(wave);
    g.clearRect(0,0,w,h);
    if(!available())return;
    const start=state.viewStart,end=start+visibleSpan();
    const mid=h/2;
    const selectedSlice=state.editingChop;
    for(let i=0;i<state.markers.length-1;i++){
      const left=Math.max(0,viewToX(state.markers[i],w)),right=Math.min(w,viewToX(state.markers[i+1],w));
      if(right<=0||left>=w)continue;
      g.fillStyle=state.removed.includes(i)?'rgba(192,85,83,.14)':i===selectedSlice?
        (isLight()?'rgba(110,132,170,.21)':'rgba(167,173,189,.22)'):
        i%2?'rgba(85,133,154,.035)':'rgba(80,150,166,.075)';
      g.fillRect(left,0,Math.max(0,right-left),h);
      if(i===selectedSlice&&right-left>62){
        g.fillStyle=isLight()?'#3a4c60':'#c9d2db';
        g.font='bold 11px monospace';
        g.fillText('CHOP '+String(i+1).padStart(2,'0'),left+9,h-12);
      }
      if(state.removed.includes(i)){
        g.fillStyle=isLight()?'rgba(246,246,247,.59)':'rgba(16,15,17,.36)';
        g.fillRect(left,0,Math.max(0,right-left),h);
        if(right-left>65){
          g.fillStyle=isLight()?'#a34b46':'#edaa98';g.font='bold 10px monospace';
          g.fillText('REMOVED',left+8,h-13);
        }
      }
    }
    // Copies are independent overlays: their trims never change the shared
    // timeline markers underneath.
    if(typeof selectedSlice==='string'){
      const current=chopRange(selectedSlice);
      if(current){
        const left=Math.max(0,viewToX(current.start,w)),right=Math.min(w,viewToX(current.end,w));
        if(right>left){
          g.fillStyle=isLight()?'rgba(69,106,201,.18)':'rgba(120,175,246,.22)';
          g.fillRect(left,0,right-left,h);
          g.fillStyle=isLight()?'#4263a7':'#c1ddff';g.font='bold 11px monospace';
          g.fillText('COPY · independent',left+9,Math.max(14,h-14));
        }
      }
    }
    g.strokeStyle=isLight()?'rgba(74,109,145,.13)':'rgba(107,132,118,.12)';g.lineWidth=1;
    for(let i=1;i<4;i++){const y=h*i/4;g.beginPath();g.moveTo(0,y);g.lineTo(w,y);g.stroke();}
    const secPerBeat=60/Number($('tempo').value||90);
    if(visibleSpan()/secPerBeat<180){
      const first=Math.ceil(start/secPerBeat);
      for(let beat=first;beat*secPerBeat<end;beat++){
        const x=viewToX(beat*secPerBeat,w);
        g.strokeStyle=beat%4===0?(isLight()?'rgba(72,106,135,.22)':'rgba(164,195,154,.18)'):
          (isLight()?'rgba(67,109,137,.09)':'rgba(109,131,119,.09)');
        g.beginPath();g.moveTo(x,0);g.lineTo(x,h);g.stroke();
      }
    }
    const peaks=state.peaks,count=peaks.length;
    g.fillStyle=isLight()?'#647c96':'#aab9c9';
    for(let x=0;x<w;x+=1.5){
      const sampleT=xToView(x,w);
      const bin=Math.min(count-1,Math.max(0,Math.floor(sampleT/state.duration*count)));
      const width=Math.max(1,Math.round(visibleSpan()/state.duration*count/w));
      let max=0;
      for(let j=bin;j<Math.min(count,bin+width);j++)max=Math.max(max,peaks[j]);
      const amp=Math.max(1,max*Math.min(mid-8,mid*.93));
      g.globalAlpha=.35+Math.min(.65,max*.8);
      g.fillRect(x,mid-amp,1.1,amp*2);
    }
    g.globalAlpha=1;
    if(state.mode!=='manual'&&state.regionStart>0){
      const width=Math.max(0,Math.min(w,viewToX(state.regionStart,w)));
      g.fillStyle=isLight()?'rgba(244,248,252,.75)':'rgba(7,12,16,.69)';g.fillRect(0,0,width,h);
    }
    if(state.mode!=='manual'&&state.regionEnd<state.duration){
      const left=Math.max(0,Math.min(w,viewToX(state.regionEnd,w)));
      g.fillStyle=isLight()?'rgba(244,248,252,.75)':'rgba(7,12,16,.69)';g.fillRect(left,0,w-left,h);
    }
    g.strokeStyle=isLight()?'rgba(42,79,104,.23)':'rgba(193,232,166,.2)';
    g.beginPath();g.moveTo(0,mid+.5);g.lineTo(w,mid+.5);g.stroke();
    state.markers.forEach((time,i)=>{
      if(time<start||time>end)return;
      const x=viewToX(time,w);const first=i===0,last=i===state.markers.length-1;
      const boundary=first||last;
      const handleX=Math.max(7,Math.min(w-7,x));
      g.strokeStyle=first?'#d8ab77':last?(isLight()?'#6e8194':'#9faebb'):
        (i-1===selectedSlice||i===selectedSlice?(isLight()?'#536f96':'#d4d9e6'):(isLight()?'#7f8b9c':'#8d9fb1'));
      g.lineWidth=first?2:boundary?1.5:1.4;
      g.beginPath();g.moveTo(x,0);g.lineTo(x,h);g.stroke();
      g.fillStyle=first?'#ddb17d':last?(isLight()?'#73859a':'#aeb9c9'):
        (i-1===selectedSlice||i===selectedSlice)?(isLight()?'#526f9c':'#d6dce9'):(isLight()?'#768da6':'#9daec1');
      g.beginPath();g.moveTo(handleX-6,2);g.lineTo(handleX+6,2);g.lineTo(handleX,14);g.closePath();g.fill();
      if(boundary){
        const labelX=first?Math.max(3,Math.min(w-58,handleX+7)):Math.max(3,Math.min(w-58,handleX-60));
        g.fillStyle=isLight()?'rgba(255,255,255,.96)':'rgba(19,23,20,.93)';
        g.fillRect(labelX,18,48,18);
        g.strokeStyle=first?'#ddbb74':'#a9cdb6';g.strokeRect(labelX+.5,18.5,47,17);
        g.fillStyle=first?(isLight()?'#956833':'#ebc898'):(isLight()?'#4d6378':'#c0ccd8');
        g.font='bold 10px monospace';g.fillText(first?'INTRO':'OUTRO',labelX+7,31);
      }
    });
    if(state.playTime>=0){
      const x=viewToX(state.playTime,w);
      if(x>=0&&x<=w){
        g.strokeStyle=state.playing?(isLight()?'#284d9f':'#fcfcf1'):(isLight()?'#7392a0':'#b6c2c2');g.lineWidth=1.5;
        g.beginPath();g.moveTo(x,0);g.lineTo(x,h);g.stroke();
        g.fillStyle=state.playing?(isLight()?'#4067b2':'#f0f8e6'):(isLight()?'#608695':'#c2cdcd');
        g.beginPath();g.arc(x,h-8,3,0,2*Math.PI);g.fill();
      }
    }
    $('rulerStart').textContent=durationString(start);
    $('rulerMid').textContent=durationString(start+visibleSpan()/2);
    $('rulerEnd').textContent=durationString(end);
    drawOverview();
    drawPadFocus();
  }
  function drawPadFocus(){
    const preview=$('padFocusWave'),track=$('padTrackOverview');
    const a=resizeCanvas(preview),b=resizeCanvas(track);
    a.g.clearRect(0,0,a.w,a.h);b.g.clearRect(0,0,b.w,b.h);
    const idx=selectedSlice();
    if(!available()||!activeSlice(idx)){
      $('padPreviewTitle').textContent='Choose an active chop to see its position';
      $('padPreviewRange').textContent='No chop selected';
      return;
    }
    const range=chopRange(idx);
    const start=range.start,end=range.end,duration=end-start;
    const assigned=state.banks.flatMap((bank,bi)=>bank.flatMap((slice,p)=>slice===idx?[bankName(bi)+(p+1)]:[]));
    $('padPreviewTitle').textContent=chopLabel(idx)+(assigned.length?' · '+assigned.slice(0,3).join(', '):' · LIBRARY ONLY');
    $('padPreviewRange').textContent=durationString(start)+' → '+durationString(end)+' · '+duration.toFixed(2)+'s';
    // Keep the viewport fixed during a drag. Otherwise changing one side
    // would re-center the mini waveform under the cursor mid-gesture.
    const window=state.miniDrag?.detailPreview || ChopTrackView.previewWindow(start,end,state.duration);
    const span=window.span,from=window.from;
    const left=Math.max(0,(start-from)/span*a.w),right=Math.min(a.w,(end-from)/span*a.w);
    a.g.fillStyle=isLight()?'rgba(111,135,170,.21)':'rgba(153,167,188,.20)';
    a.g.fillRect(left,0,Math.max(0,right-left),a.h);
    const mid=a.h/2,peaks=state.peaks||[];
    a.g.fillStyle=isLight()?'#5e7f9c':'#b2bfce';
    for(let x=0;x<a.w;x+=2){
      const t=from+x/a.w*span;
      const i=Math.max(0,Math.min(peaks.length-1,Math.floor(t/state.duration*peaks.length)));
      const amplitude=Math.max(.7,(peaks[i]||0)*(mid-6));
      a.g.fillRect(x,mid-amplitude,1.4,amplitude*2);
    }
    a.g.strokeStyle=isLight()?'#627f9f':'#c6d0df';a.g.lineWidth=1.5;
    a.g.beginPath();a.g.moveTo(left,0);a.g.lineTo(left,a.h);a.g.moveTo(right,0);a.g.lineTo(right,a.h);a.g.stroke();
    // Large side handles remain easy to grab with a mouse or trackpad.
    function handle(g,x,height,color,text,labelSide){
      g.fillStyle=color;
      g.fillRect(x-5,0,10,height);
      g.fillStyle=isLight()?'#283846':'#35383c';
      g.fillRect(x-1,height*.28,2,height*.44);
      if(text){
        const tx=labelSide==='left'?Math.max(4,x+8):Math.max(4,x-43);
        g.fillStyle=isLight()?'#315f7c':'#d9dce5';g.font='bold 10px monospace';
        g.fillText(text,tx,Math.min(height-4,14));
      }
    }
    handle(a.g,left,a.h,'#d5ba79','START','left');
    handle(a.g,right,a.h,isLight()?'#7794bd':'#c6cada','END','right');
    if(state.playing&&state.playTime>=start&&state.playTime<=end){
      const x=(state.playTime-from)/span*a.w;
      a.g.strokeStyle=isLight()?'#305cbb':'#fff7db';a.g.lineWidth=2;
      a.g.beginPath();a.g.moveTo(x,0);a.g.lineTo(x,a.h);a.g.stroke();
    }
    b.g.fillStyle=isLight()?'#5e748d':'#92a2b4';
    for(let x=0;x<b.w;x+=3){
      const i=Math.min(peaks.length-1,Math.floor(x/b.w*peaks.length));
      const amp=Math.max(.5,(peaks[i]||0)*b.h*.44);
      b.g.fillRect(x,b.h/2-amp,1.4,amp*2);
    }
    const regionStart=start/state.duration*b.w,regionEnd=end/state.duration*b.w;
    b.g.fillStyle=isLight()?'rgba(93,121,168,.24)':'rgba(176,188,209,.28)';
    b.g.fillRect(regionStart,0,Math.max(3,regionEnd-regionStart),b.h);
    b.g.strokeStyle=isLight()?'#5d7a9e':'#b7c3d3';b.g.lineWidth=1.2;
    b.g.strokeRect(regionStart+.5,.5,Math.max(3,regionEnd-regionStart),b.h-1);
    // Mirrored start/end handles on the full-track mini overview.
    b.g.fillStyle='#e6be79';b.g.fillRect(Math.max(0,regionStart-3),0,6,b.h);
    b.g.fillStyle=isLight()?'#7794bd':'#c6cada';b.g.fillRect(Math.min(b.w-5,Math.max(0,regionEnd-3)),0,6,b.h);
    if(state.playing){
      const x=state.playTime/state.duration*b.w;
      b.g.strokeStyle=isLight()?'#416bd2':'#f7f7e7';b.g.lineWidth=1.2;
      b.g.beginPath();b.g.moveTo(x,0);b.g.lineTo(x,b.h);b.g.stroke();
    }
  }
  function drawOverview(){
    const {g,w,h}=resizeCanvas(overview);
    g.clearRect(0,0,w,h);
    if(!available())return;
    const count=state.peaks.length;
    g.fillStyle=isLight()?'#627c99':'#a4b2c3';
    for(let x=0;x<w;x+=2){
      const p=state.peaks[Math.min(count-1,Math.floor(x/w*count))];
      const a=Math.max(.7,p*(h*.42));
      g.fillRect(x,h/2-a,1,a*2);
    }
    if(state.mode!=='manual'){
      g.fillStyle='rgba(135,148,171,.25)';
      g.fillRect(state.regionStart/state.duration*w,0,Math.max(2,(state.regionEnd-state.regionStart)/state.duration*w),h);
    }
    const left=state.viewStart/state.duration*w,width=visibleSpan()/state.duration*w;
    g.fillStyle=isLight()?'rgba(89,116,156,.15)':'rgba(160,177,205,.16)';g.fillRect(left,0,width,h);
    g.strokeStyle=isLight()?'#5f82ae':'#b4c5d9';g.lineWidth=1;g.strokeRect(left+.5,.5,Math.max(1,width-1),h-1);
  }
  function renderPads(){
    if(!state.banks.length)state.banks=[ChopCollection.emptyBank()];
    padContainer.replaceChildren();
    for(let pad=0;pad<16;pad++){
      const slice=getSliceIndex(pad),exists=available()&&activeSlice(slice);
      const cell=document.createElement('div');cell.className='pad-cell';
      const el=document.createElement('button');el.type='button';el.className='pad'+(!exists?' empty':'')+(exists?' assigned-chop':'')+(state.selected===pad?' selected':'')+(state.playingPad===pad&&state.playing?' active':'');
      el.setAttribute('aria-label','Pad '+(pad+1)+(exists?', '+chopLabel(slice):', empty'));
      const top=document.createElement('div');top.className='pad-top';
      const num=document.createElement('span');num.className='pad-number';num.textContent='PAD '+String(pad+1).padStart(2,'0');
      const note=document.createElement('span');note.className='pad-note';note.textContent=ChopExport.midiNoteName(36+pad);
      top.append(num,note);
      const c=document.createElement('canvas');c.width=140;c.height=34;c.className='pad-wave';
      const info=document.createElement('div');info.className='pad-info';
      const range=chopRange(slice);
      const title=document.createElement('span');title.className='pad-title';title.textContent=exists?chopLabel(slice):'Empty pad';
      const length=document.createElement('span');length.className='pad-length';length.textContent=exists?(range.end-range.start).toFixed(2)+'s':'—';
      info.append(title,length);el.append(top,c,info);
      if(exists){
        const pen=c.getContext('2d');pen.fillStyle=(isLight()?PALETTE_LIGHT:PALETTE)[pad];pen.globalAlpha=.85;
        const first=Math.floor(range.start/state.duration*state.peaks.length);
        const last=Math.min(state.peaks.length,Math.ceil(range.end/state.duration*state.peaks.length));
        for(let x=0;x<140;x+=2){
          const k=Math.min(last-1,first+Math.floor(x/140*(last-first)));
          const a=Math.max(1,(state.peaks[Math.max(0,k)]||0)*15);
          pen.fillRect(x,17-a,1,a*2);
        }
      }
      el.addEventListener('click',()=>{if(state.midiLearning>=0){state.midiLearning=pad;$('midiStatus').textContent='Learning pad '+(pad+1)+': press the matching MIDI controller pad now.';selectPad(pad);return;}playPad(pad);});
      if(exists){
        // A pad's body copies its chop into the arranger; the separate ⠿
        // handle still swaps pad assignments without touching the song.
        el.draggable=true;
        el.title='Click to preview · Drag onto an arrangement track to place a clip';
        el.addEventListener('dragstart',e=>{
          e.dataTransfer.effectAllowed='copy';
          // Keep an in-page fallback reference for browsers that omit custom
          // MIME types during HTML5 dragover; no source/pad state is changed.
          window.__CHOPROOM_DRAG_REF={ref:slice};
          e.dataTransfer.setData('application/x-choproom-ref',String(slice));
          e.dataTransfer.setData('text/plain','choproom-ref:'+String(slice));
        });
        el.addEventListener('dragend',()=>{window.__CHOPROOM_DRAG_REF=null;});
      }
      const handle=document.createElement('button');handle.type='button';
      handle.className='pad-move';handle.textContent='⠿';
      handle.setAttribute('aria-label','Move or swap Bank '+bankName(state.activeBank)+' Pad '+(pad+1));
      handle.title='Drag to another pad to move or swap. Click this grip and then another to swap.';
      cell.append(el,handle);
      wirePadDrag(handle,cell,state.activeBank,pad);
      padContainer.append(cell);
    }
    $('currentBankName').textContent='BANK '+bankName(state.activeBank);
    $('currentBankCount').textContent=state.padMap.filter(i=>activeSlice(i)).length+' / 16 LOADED';
    renderOrganizer();
    window.ChopDevices?.renderPads?.();
  }
  function cleanupPadDrag(){
    state.padDrag=null;
    document.querySelectorAll('.drag-source,.drop-target').forEach(el=>el.classList.remove('drag-source','drop-target'));
  }
  function movePadAssignment(fromBank,fromPad,toBank,toPad){
    stopAudio();
    const result=ChopCollection.movePad(state.banks,fromBank,fromPad,toBank,toPad);
    if(result.error)return toast('Could not move pad: '+result.error,true);
    state.activeBank=toBank;state.targetBank=toBank;state.targetPad=toPad;
    state.selected=toPad;
    state.editingChop=state.banks[toBank][toPad]??null;
    state.padPick=null;cleanupPadDrag();redrawCollection();
    toast(result.swapped
      ?'Swapped '+bankName(fromBank)+(fromPad+1)+' and '+bankName(toBank)+(toPad+1)
      :'Moved to Bank '+bankName(toBank)+' · Pad '+(toPad+1));
  }
  function wirePadDrag(handle,target,bankIndex,padIndex){
    const ref=state.banks[bankIndex][padIndex];
    handle.draggable=activeSlice(ref);
    handle.disabled=!activeSlice(ref);
    handle.addEventListener('click',e=>{
      e.preventDefault();e.stopPropagation();
      if(!activeSlice(ref))return;
      if(!state.padPick){
        state.padPick={bank:bankIndex,pad:padIndex};
        toast('Picked '+bankName(bankIndex)+(padIndex+1)+'. Click another grip to swap, or drag it.');
      }else{
        const from=state.padPick;state.padPick=null;
        if(from.bank===bankIndex&&from.pad===padIndex){toast('Pad move canceled.');return;}
        movePadAssignment(from.bank,from.pad,bankIndex,padIndex);
      }
    });
    handle.addEventListener('dragstart',e=>{
      if(!activeSlice(ref)){e.preventDefault();return;}
      state.padPick=null;state.padDrag={bank:bankIndex,pad:padIndex};
      e.dataTransfer.effectAllowed='move';
      e.dataTransfer.setData('text/plain','choproom-pad');
      target.classList.add('drag-source');
    });
    handle.addEventListener('dragend',cleanupPadDrag);
    target.addEventListener('dragover',e=>{
      if(!state.padDrag)return;
      e.preventDefault();e.dataTransfer.dropEffect='move';
      target.classList.add('drop-target');
    });
    target.addEventListener('dragleave',e=>{
      if(!target.contains(e.relatedTarget))target.classList.remove('drop-target');
    });
    target.addEventListener('drop',e=>{
      if(!state.padDrag)return;
      e.preventDefault();e.stopPropagation();
      const from=state.padDrag;
      movePadAssignment(from.bank,from.pad,bankIndex,padIndex);
    });
  }
  function renderOrganizer(){
    const root=$('organizerBanks');
    root.replaceChildren();
    if(root.closest('.organizer-wrap').dataset.collapsed==='true')return;
    state.banks.forEach((bank,bi)=>{
      const section=document.createElement('section');
      const heading=document.createElement('div');heading.className='organizer-bank-title';
      heading.textContent='BANK '+bankName(bi);
      const grid=document.createElement('div');grid.className='organizer-grid';
      bank.forEach((ref,pad)=>{
        const cell=document.createElement('div');
        cell.className='organizer-slot'+(!activeSlice(ref)?' empty':'');
        const num=document.createElement('span');num.className='organizer-slot-label';num.textContent='PAD '+String(pad+1).padStart(2,'0');
        const label=document.createElement('span');label.className='organizer-slot-chop';label.textContent=activeSlice(ref)?chopLabel(ref):'Empty';
        const grip=document.createElement('button');grip.type='button';grip.className='organizer-grip';grip.textContent='⠿';
        grip.title='Drag or click grip to move/swap this pad';
        grip.setAttribute('aria-label','Move Bank '+bankName(bi)+' Pad '+(pad+1));
        cell.append(num,label,grip);wirePadDrag(grip,cell,bi,pad);
        cell.addEventListener('click',e=>{
          if(e.target.closest('button'))return;
          state.activeBank=bi;state.padMap=state.banks[bi];state.selected=pad;
          state.editingChop=activeSlice(ref)?ref:null;redrawCollection();
        });
        grid.append(cell);
      });
      section.append(heading,grid);root.append(section);
    });
  }
  function bankName(i){
    let n=i+1,out='';
    while(n>0){n--;out=String.fromCharCode(65+n%26)+out;n=Math.floor(n/26);}
    return out;
  }
  function renderBanks(){
    const root=$('bankTabs');root.replaceChildren();
    state.banks.forEach((bank,i)=>{
      const b=document.createElement('button');b.type='button';
      b.className='bank-tab'+(i===state.activeBank?' active':'');
      b.textContent='BANK '+bankName(i);
      b.setAttribute('aria-pressed',String(i===state.activeBank));
      b.addEventListener('click',()=>{
        stopAudio();state.activeBank=i;state.padMap=state.banks[i];state.selected=0;
        state.targetBank=i;state.targetPad=0;
        const first=state.padMap.find(t=>activeSlice(t));
        state.editingChop=first??null;
        redrawCollection();
      });
      root.append(b);
    });
    const target=$('vaultTargetBank');target.replaceChildren();
    for(let i=0;i<state.banks.length;i++){
      const option=document.createElement('option');option.value=String(i);option.textContent='Bank '+bankName(i);
      target.append(option);
    }
    state.targetBank=Math.max(0,Math.min(state.targetBank,state.banks.length-1));
    target.value=String(state.targetBank);
    const slots=$('vaultTargetPad');
    if(!slots.options.length)for(let i=0;i<16;i++){
      const option=document.createElement('option');option.value=String(i);option.textContent=String(i+1).padStart(2,'0');
      slots.append(option);
    }
    slots.value=String(state.targetPad);
  }
  function renderVault(){
    $('vaultTargetBank').value=String(state.targetBank);
    $('vaultTargetPad').value=String(state.targetPad);
    const root=$('vaultItems'),scrollPosition=root.scrollTop;
    root.replaceChildren();
    const showRemoved=$('showRemoved').checked;
    const refs=allChopRefs();
    const count=refs.length;
    $('vaultTotal').textContent=count+' CHOPS ('+state.variants.length+' COPIES) · '+state.removed.length+' REMOVED · '+state.banks.length+' BANK'+(state.banks.length===1?'':'S');
    let shown=0;
    for(const i of refs){
      const removed=state.removed.includes(i),range=chopRange(i);
      if(removed&&!showRemoved)continue;
      shown++;
      const row=document.createElement('div');
      row.className='vault-row'+(removed?' removed':'')+(state.editingChop===i?' selected':'');
      const name=document.createElement('div');
      const assigned=state.banks.flatMap((bank,b)=>bank.flatMap((slice,p)=>slice===i?[''+bankName(b)+(p+1)]:[]));
      const heading=document.createElement('div');heading.className='vault-name';
      heading.textContent=chopLabel(i)+(removed?' · REMOVED':'');
      const meta=document.createElement('div');meta.className='vault-meta';
      meta.textContent=durationString(range.start)+' – '+durationString(range.end)+' · '+(range.end-range.start).toFixed(2)+'s'+(assigned.length?' · '+assigned.slice(0,3).join(', '):' · Unassigned');
      name.append(heading,meta);
      const actions=document.createElement('div');actions.className='vault-actions';
      function action(title,cls,cb){const b=document.createElement('button');b.type='button';b.textContent=title;b.className=cls;b.addEventListener('click',cb);actions.append(b);}
      if(removed){
        action('↶ Restore','vault-restore',()=>restoreChop(i));
      }else{
        action('▶ Preview','',()=>playChop(i));
        action('Assign → '+bankName(state.targetBank)+String(state.targetPad+1).padStart(2,'0'),'',()=>assignChop(i,state.targetPad,state.targetBank));
        action('→ Timeline','',()=>{
          if(!window.ChopArrangerUI?.addChop?.(i))return toast('Could not add this chop to the arranger.',true);
          toast(chopLabel(i)+' placed on the timeline. Drag it to move it or leave a gap.');
          $('arrangerWorkspace')?.scrollIntoView({behavior:'smooth',block:'center'});
        });
        action('⧉ Duplicate','vault-duplicate',()=>duplicateChop(i));
        action('✂ Split 50/50','vault-split',()=>splitChop(i));
        action('Remove','vault-remove',()=>deleteChop(i));
      }
      row.addEventListener('click',e=>{if(e.target.closest('button'))return;chooseChop(i);});
      row.append(name,actions);root.append(row);
    }
    if(!shown){const empty=document.createElement('div');empty.className='vault-empty';empty.textContent='No chops here yet. Use Slicer to add cuts, or enable Show removed.';root.append(empty);}
    root.scrollTop=scrollPosition;
  }
  function assignChop(index,pad,bankIndex=state.activeBank){
    if(!activeSlice(index))return;
    state.banks[bankIndex][pad]=index;state.activeBank=bankIndex;state.selected=pad;state.editingChop=index;
    state.targetBank=bankIndex;state.targetPad=pad;
    redrawCollection();toast(chopLabel(index)+' assigned to Bank '+bankName(bankIndex)+' · Pad '+(pad+1));
  }
  function activateNewVariant(item){
    const location=ChopCollection.assignFirstAvailable(state.banks,item.id);
    state.activeBank=location.bank;state.padMap=state.banks[location.bank];
    state.selected=location.pad;
    state.targetBank=location.bank;state.targetPad=location.pad;
    state.editingChop=item.id;
    return location;
  }
  function duplicateChop(ref=selectedSlice()){
    if(!activeSlice(ref))return toast('Select an active chop to duplicate.',true);
    if(allChopRefs().length>=512)return toast('This song has reached the 512-chop library limit.',true);
    stopAudio();
    const copy=ChopVariants.copyRange(ref,state.markers,state.variants);
    if(!copy)return toast('Unable to duplicate this chop.',true);
    state.settings[copy.id]={...(state.settings[ref]||{})};
    const location=activateNewVariant(copy);
    redrawCollection();
    toast(copy.label+' created in Bank '+bankName(location.bank)+' Pad '+(location.pad+1)+'. Trim this copy freely.');
  }
  function splitChop(ref=selectedSlice(),position=null){
    if(!activeSlice(ref))return toast('Select an active chop to split.',true);
    if(allChopRefs().length>510)return toast('Not enough library room for two additional chops.',true);
    const range=chopRange(ref);
    if(range.end-range.start<.08)return toast('Chop is too short for splitting. At least 0.08s is needed.',true);
    if(position!=null&&!(position>range.start+.03&&position<range.end-.03)){
      return toast('Place the playhead inside the selected chop, away from its endpoints.',true);
    }
    stopAudio();
    const pieces=ChopVariants.splitRange(ref,state.markers,state.variants,position);
    if(!pieces)return toast('This chop cannot be split at that location.',true);
    const opts={...(state.settings[ref]||{})};
    const locations=pieces.map(piece=>{
      state.settings[piece.id]={...opts};
      return ChopCollection.assignFirstAvailable(state.banks,piece.id);
    });
    const first=locations[0];state.activeBank=first.bank;state.padMap=state.banks[first.bank];
    state.selected=first.pad;state.targetBank=first.bank;state.targetPad=first.pad;
    state.editingChop=pieces[0].id;
    redrawCollection();
    toast('Created two independent pieces from '+chopLabel(ref)+'. The full chop is unchanged.');
  }
  function keepFirstHalfOfCopy(){
    const ref=selectedSlice();
    if(typeof ref!=='string'||!activeSlice(ref))return toast('Duplicate the original chop first; then trim the independent copy.',true);
    const range=chopRange(ref);
    if(range.end-range.start<.08)return toast('This copy is too short to halve.',true);
    stopAudio();
    ChopVariants.trimCopy(ref,state.markers,state.variants,'end',(range.start+range.end)/2,state.duration);
    redrawCollection();
    toast(chopLabel(ref)+' now plays only its first half. The original and other copies are unchanged.');
  }
  function createBank(){
    state.banks.push(ChopCollection.emptyBank());
    state.activeBank=state.banks.length-1;
    state.selected=0;state.editingChop=null;
    state.targetBank=state.activeBank;
    state.targetPad=0;
    redrawCollection();
    toast('Bank '+bankName(state.activeBank)+' created. Assign any saved chop to its pads from the Chop Library.');
  }
  function deleteChop(index){
    if(!activeSlice(index)){
      if(state.removed.includes(index))toast('This chop is already removed. Use Show removed in the library to restore it.');
      return;
    }
    stopAudio();ChopCollection.removeChop(state.banks,state.removed,index);
    if(state.editingChop===index){
      state.editingChop=allChopRefs().find(i=>activeSlice(i))??null;
    }
    redrawCollection();toast(chopLabel(index)+' removed from the working set. Enable Show removed to restore it.');
  }
  function deleteAtTime(time){
    if(!available()||time<state.markers[0]||time>=state.markers[state.markers.length-1]){
      toast('No chop at this position. Select a marked chop to remove.');
      return;
    }
    deleteChop(chopAtTime(time));
  }
  function restoreChop(index){
    if(!state.removed.includes(index))return;
    ChopCollection.restoreChop(state.banks,state.removed,index);
    state.editingChop=index;redrawCollection();toast(chopLabel(index)+' restored.');
  }
  function updateSelection(){
    const index=selectedSlice();
    const controls=['startAt','endAt','pitch','fade','reversePad','resetPad','duplicateChop','splitChop'];
    if(!activeSlice(index)){
      $('selectedIndex').textContent='EMPTY PAD '+String(state.selected+1).padStart(2,'0');
      $('startAt').value='';$('endAt').value='';
      $('selectionLength').textContent='Assign a chop from the library';
      $('selectionNote').textContent='Bank '+bankName(state.activeBank)+' · Pad '+(state.selected+1);
      for(const id of controls)$(id).disabled=true;
      $('makeHalfChop').disabled=true;
      return;
    }
    for(const id of controls)$(id).disabled=false;
    const range=chopRange(index);
    const first=range.start,second=range.end;
    $('selectedIndex').textContent=chopLabel(index);
    $('startAt').value=first.toFixed(3);$('endAt').value=second.toFixed(3);
    $('selectionLength').textContent='Duration '+Math.max(0,second-first).toFixed(2)+'s';
    const currentPad=state.padMap.indexOf(index);
    $('selectionNote').textContent=currentPad<0?'Library chop · unassigned in this bank'
      :ChopExport.midiNoteName(36+currentPad)+' · MIDI '+(36+currentPad);
    const opts=state.settings[index]||{};
    $('pitch').value=opts.pitch||0;$('pitchLabel').textContent=(opts.pitch>0?'+':'')+(opts.pitch||0)+' ST';
    $('fade').value=opts.fadeMs==null?8:opts.fadeMs;$('fadeLabel').textContent=$('fade').value+' MS';
    $('reversePad').checked=Boolean(opts.reverse);
    $('makeHalfChop').disabled=typeof index!=='string'||second-first<.08;
    $('copyDescription').textContent=typeof index==='string'
      ?'Independent copy: trim or split it without changing its original.'
      :'Duplicate this chop to keep a full version while creating shorter variations.';
  }
  function changeMarkers(newMarkers){
    if(!available())return;
    stopAudio();
    const lo=state.regionStart,hi=state.regionEnd;
    const cleaned=[lo,...newMarkers.filter(x=>Number.isFinite(x)&&x>lo+.025&&x<hi-.025).sort((a,b)=>a-b),hi];
    const dedup=[];
    for(const m of cleaned)if(!dedup.length||m-dedup[dedup.length-1]>.025)dedup.push(m);
    if(dedup.length>513)dedup.length=513;
    if(dedup[dedup.length-1]!==hi)dedup[dedup.length-1]=hi;
    state.markers=dedup;
    // Automatic recutting replaces the original timeline only. Independent
    // copies survive because their absolute start/end are not marker-based.
    state.settings=Object.fromEntries(Object.entries(state.settings).filter(([key])=>ChopVariants.isVariantId(key)));
    state.removed=state.removed.filter(ref=>typeof ref==='string');
    state.banks=[ChopCollection.emptyBank()];state.activeBank=0;
    for(let i=0;i<state.markers.length-1;i++)ChopCollection.assignFirstAvailable(state.banks,i);
    for(const item of state.variants)if(!state.removed.includes(item.id))ChopCollection.assignFirstAvailable(state.banks,item.id);
    state.padMap=state.banks[0];state.selected=0;state.editingChop=0;
    redrawCollection();
  }
  function gridChop(){
    if(!available())return toast('Load a sample first.',true);
    const length=state.regionEnd-state.regionStart;
    changeMarkers(Array.from({length:state.gridCount-1},(_,i)=>state.regionStart+length*(i+1)/state.gridCount));
    toast('Created '+state.gridCount+' evenly spaced chops. Drag the markers to refine.');
  }
  function transientChop(){
    if(!available())return;
    const key=state.fileName+'|'+state.regionStart+'|'+state.regionEnd;
    if(state.transientRegionKey!==key||!state.transientCandidates){
      state.transientCandidates=ChopSlicing.findCandidates(state.audio,state.regionStart,state.regionEnd);
      state.transientRegionKey=key;
    }
    const marks=ChopSlicing.markersForPercent(state.transientCandidates,Number($('sensitivity').value));
    changeMarkers(marks);
    $('detectedCount').textContent=marks.length+' CUT'+(marks.length===1?'':'S')+' · '+(marks.length+1)+' PAD'+(marks.length===0?'':'S');
  }
  function syncModeUI(){
    const mode=state.mode;
    document.querySelectorAll('[data-mode]').forEach(el=>el.classList.toggle('active',el.dataset.mode===mode));
    document.querySelectorAll('.mode-group').forEach(group=>group.classList.toggle('active',group.querySelector('[data-mode]')?.dataset.mode===mode));
    $('gridDetail').classList.toggle('hidden',mode!=='grid');
    $('transientDetail').classList.toggle('hidden',mode!=='transient');
    $('manualDetail').classList.toggle('hidden',mode!=='manual');
    $('placePercentCut').disabled=!available()||mode!=='manual'||!activeSlice(state.editingChop);
    $('focusHeading').textContent=mode==='manual'?'ZOOM / NAVIGATE':'FOCUS A PHRASE';
    syncToolUI();
  }
  function syncToolUI(){
    const slicing=state.tool==='slice',deleting=state.tool==='delete';
    for(const [id,active] of [['seekTool',!slicing&&!deleting],['slicerTool',slicing],['deleteTool',deleting]]){
      const el=$(id);
      el.classList.toggle('active',active);
      el.classList.toggle('delete-active',id==='deleteTool'&&active);
      el.setAttribute('aria-pressed',String(active));
    }
    $('waveHint').textContent=deleting?'Delete ON · click a chop to remove · restore from library'
      :slicing?'Slicer ON · click to cut · drag marker handles':'Seek ON · click to listen · drag existing markers';
    $('addAtPlayhead').disabled=!available()||!slicing;
    $('deleteCut').disabled=!available()||state.markers.length<=2;
    $('clearCuts').disabled=!available()||state.mode!=='manual';
    $('waveArea').classList.toggle('slicer-active',slicing);
    $('waveArea').classList.toggle('delete-active',deleting);
    if(wave.style.cursor!=='ew-resize')wave.style.cursor=slicing?'crosshair':deleting?'cell':'pointer';
  }
  function setTool(tool){
    if(!['seek','slice','delete'].includes(tool))return;
    state.tool=tool;
    syncToolUI();
    toast(tool==='slice'?'Slicer active. Click to add a cut.'
      :tool==='delete'?'Delete active. Click a chop to remove it; restore it from the Chop Library.'
      :'Seek active. Click to move playback without creating any cuts.');
  }
  function setMode(mode){
    if(mode===state.mode)return;
    state.mode=mode;
    syncModeUI();
    if(!available())return;
    if(mode==='manual'){
      // Never discard the user’s existing chops when switching modes.
      // The old implementation called changeMarkers([]), erasing their
      // carefully positioned boundaries and the saved pad assignment.
      // If an automatic mode was scoped to a short region, add new outside
      // regions while retaining each original boundary and pad assignment.
      let expanded=false;
      if(state.markers[0]>.03){
        const oldCount=state.markers.length;
        state.markers.unshift(0);
        const shifted=ChopCollection.shiftForInsertion(state.banks,state.removed,0,oldCount);
        state.removed=shifted.removed;
        const settings={};
        for(const [key,v] of Object.entries(state.settings)){
          settings[/^\d+$/.test(key)?String(Number(key)+1):key]=v;
        }
        state.settings=settings;
        if(Number.isInteger(state.editingChop))state.editingChop+=1;
        expanded=true;
      }
      if(state.markers[state.markers.length-1]<state.duration-.03){
        const next=state.markers.length-1;
        state.markers.push(state.duration);
        ChopCollection.assignFirstAvailable(state.banks,next);
        expanded=true;
      }
      state.regionStart=0;
      state.regionEnd=state.duration;
      state.padMap=state.banks[state.activeBank];
      state.zoom=Math.max(1,Math.min(12,state.duration/(8*4*60/Number($('tempo').value||90))*.9));
      state.viewStart=Math.max(0,state.playTime-visibleSpan()/4);
      clampView();
      $('zoomAmount').textContent=state.zoom.toFixed(1)+'×';
      if(expanded)redrawCollection();
      else{updateRegionText();drawWave();}
      toast('Free Cut ready. Existing chops preserved. Seek never cuts; use Slicer or the explicit ✂ button.');
    }else if(mode==='transient'){
      transientChop();
      toast('Adjust the percentage. Chop markers and pads now update live.');
    }
  }
  function addMarker(time){
    if(!available())return;
    const result=ChopMarkers.insertCut(state.markers,time,state.duration);
    if(result.error==='full')return toast('Chop Library has reached 512 cuts for this song.',true);
    if(result.error==='duplicate')return toast('There is already a cut at this spot. Drag its handle to adjust it.',true);
    if(result.error==='tooClose')return toast('Move a little farther from the neighboring cut (minimum 0.03 seconds).',true);
    if(result.error)return toast('Place cuts anywhere within the song.',true);
    const oldSettings=state.settings;
    const shifted={};
    for(const [key,options] of Object.entries(oldSettings)){
      const oldIndex=Number(key);
      shifted[oldIndex>=result.index?oldIndex+1:oldIndex]=options;
    }
    state.settings=shifted;
    const addition=ChopCollection.shiftForInsertion(state.banks,state.removed,result.index,state.markers.length);
    state.removed=addition.removed;
    state.markers=result.markers;
    state.regionStart=state.markers[0];
    state.regionEnd=state.markers[state.markers.length-1];
    state.transientCandidates=null;state.transientRegionKey='';
    state.activeBank=addition.assigned.bank;
    state.padMap=state.banks[state.activeBank];state.selected=addition.assigned.pad;
    state.editingChop=addition.freshSlice;
    redrawCollection();
  }
  function manualCutLocation(){
    if(!available())return null;
    const ref=state.editingChop;
    const r=activeSlice(ref)?chopRange(ref):null;
    const range=r||{start:state.markers[0],end:state.markers[state.markers.length-1]};
    const pct=Math.max(1,Math.min(99,Number($('manualCutPercent').value)||50))/100;
    return range.start+(range.end-range.start)*pct;
  }
  function previewManualCut(){
    $('manualCutText').textContent=$('manualCutPercent').value+'%';
    const t=manualCutLocation();
    if(t==null)return;
    // Nudge the playhead but never cut on slider input or keyboard arrows.
    state.playTime=t;
    $('transportTime').innerHTML=durationString(t)+' <span>/ '+durationString(state.duration)+'</span>';
    state.viewStart=t-visibleSpan()/2;clampView();drawWave();
  }
  function removeMarker(){
    const slice=selectedSlice();
    if(!activeSlice(slice))return toast('Choose an existing chop before merging cut boundaries.',true);
    if(typeof slice==='string')return deleteChop(slice);
    if(state.markers.length<=2)return toast('Nothing to remove.');
    const index=slice===0?1:slice;
    state.removed=ChopCollection.shiftForMerge(state.banks,state.removed,index);
    state.markers.splice(index,1);state.padMap=Array.from({length:16},(_,i)=>i);
    const nextSettings={};
    for(const [k,v] of Object.entries(state.settings)){
      if(ChopVariants.isVariantId(k)){nextSettings[k]=v;continue;}
      const i=Number(k);if(i===index)continue;
      nextSettings[i>index?i-1:i]=v;
    }
    state.settings=nextSettings;
    state.padMap=state.banks[state.activeBank];
    state.editingChop=Math.max(0,Math.min(state.markers.length-2,index-1));
    stopAudio();redrawCollection();
  }
  function updateSliceBoundary(side){
    if(!activeSlice(selectedSlice()))return;
    const ref=selectedSlice();
    const input=$(side==='start'?'startAt':'endAt');
    const value=Number(input.value);
    if(typeof ref==='string'){
      const range=chopRange(ref);
      if(!Number.isFinite(value)||value<0||value>state.duration
        ||(side==='start'&&value>=range.end-.03)
        ||(side==='end'&&value<=range.start+.03)){
        toast('Copy start/end must stay within the song with at least 0.03s between them.',true);
        updateSelection();return;
      }
      stopAudio();
      ChopVariants.trimCopy(ref,state.markers,state.variants,side,value,state.duration);
      redrawCollection();return;
    }
    const index=ref,i=side==='start'?index:index+1;
    const clamped=ChopMarkers.clampBoundary(state.markers,i,value,state.duration);
    if(clamped==null||Math.abs(clamped-value)>.00001){toast('Boundary must stay inside the song and away from neighboring cuts.',true);updateSelection();return;}
    moveMarkerTo(i,value);renderPads();persistSession();
  }
  function moveMarkerTo(index,time){
    const value=ChopMarkers.clampBoundary(state.markers,index,time,state.duration);
    if(value==null)return;
    state.markers[index]=value;
    if(index===0){state.regionStart=value;state.transientCandidates=null;state.transientRegionKey='';}
    if(index===state.markers.length-1){state.regionEnd=value;state.transientCandidates=null;state.transientRegionKey='';}
    updateRegionText();updateSelection();drawWave();
  }
  function moveChopBoundary(ref,side,time){
    if(typeof ref==='string'){
      const updated=ChopVariants.trimCopy(ref,state.markers,state.variants,side,time,state.duration);
      if(updated){updateSelection();drawWave();}
    }else if(Number.isInteger(ref)){
      moveMarkerTo(ref+(side==='end'?1:0),time);
    }
  }
  function doZoom(factor){
    if(!available())return;
    const middle=state.viewStart+visibleSpan()/2;
    state.zoom=Math.max(1,Math.min(12,state.zoom*factor));
    state.viewStart=middle-visibleSpan()/2;clampView();
    $('zoomAmount').textContent=state.zoom.toFixed(1)+'×';drawWave();
  }
  function updateRegionText(){
    if(!available())return;
    if(state.mode==='manual'){
      $('regionText').textContent='Full '+durationString(state.duration)+' song editable · cuts '+Math.max(0,state.markers.length-2)+' · last boundary '+durationString(state.markers[state.markers.length-1]||0);
      return;
    }
    const full=state.regionStart===0&&Math.abs(state.regionEnd-state.duration)<.001;
    $('regionText').textContent=full?'Full song · '+durationString(state.duration):durationString(state.regionStart)+' – '+durationString(state.regionEnd)+' · '+(state.regionEnd-state.regionStart).toFixed(1)+'s';
  }
  function focusBars(bars){
    if(!available())return toast('Load audio first.',true);
    if(state.mode==='manual'){
      // Focusing a phrase only changes what the waveform shows. The previous
      // implementation discarded all manual cuts and moved their boundaries.
      if(bars<=0){state.zoom=1;state.viewStart=0;}
      else{
        const span=bars*4*(60/Math.max(40,Number($('tempo').value)||90));
        state.zoom=Math.max(1,Math.min(12,state.duration/span));
        state.viewStart=state.playTime;
        clampView();
      }
      $('zoomAmount').textContent=state.zoom.toFixed(1)+'×';
      drawWave();
      toast(bars<=0?'Viewing the whole song. All cuts are preserved.':'Zoomed to '+bars+' bars. All cuts are preserved; slice anywhere.');
      return;
    }
    stopAudio();
    if(bars<=0){
      state.regionStart=0;state.regionEnd=state.duration;
      state.zoom=1;state.viewStart=0;
    }else{
      const span=bars*4*(60/Math.max(40,Number($('tempo').value)||90));
      state.regionStart=Math.max(0,Math.min(state.playTime,Math.max(0,state.duration-.5)));
      state.regionEnd=Math.min(state.duration,state.regionStart+span);
      if(state.regionEnd-state.regionStart<.1)return toast('Choose an earlier point in the song.',true);
      state.zoom=Math.max(1,Math.min(12,state.duration/(state.regionEnd-state.regionStart)*.9));
      state.viewStart=state.regionStart;
      clampView();
    }
    $('zoomAmount').textContent=state.zoom.toFixed(1)+'×';
    state.transientCandidates=null;state.transientRegionKey='';
    updateRegionText();
    if(state.mode==='transient')transientChop();
    else if(state.mode==='manual')changeMarkers([]);
    else gridChop();
    toast(bars<=0?'Selected the full source audio.':'Focused '+bars+' bars starting at '+durationString(state.regionStart)+'.');
  }
  function download(blob,file){
    const link=document.createElement('a'),url=URL.createObjectURL(blob);
    link.href=url;link.download=file;document.body.append(link);link.click();link.remove();
    setTimeout(()=>URL.revokeObjectURL(url),30000);
  }
  function exportKit(){
    if(!available())return toast('Load a sound to export.',true);
    const name=ChopExport.cleanName(state.fileName.replace(/\.[^.]+$/,''));
    const button=$('exportAll'),button2=$('exportBottom');
    button.disabled=true;button2.disabled=true;
    toast('Preparing WAV slices and MIDI in your browser…');
    setTimeout(()=>{
      try{
        const result=ChopExport.makeCollectionKit(state.audio,state.markers,state.banks,state.removed,state.settings,
          {name,bpm:Number($('tempo').value),variants:state.variants});
        download(result.blob,name+'_CHOPROOM_Ableton_Kit.zip');
        toast('Exported '+result.count+' active chops across '+result.banks.length+' pad banks, with MIDI and SFZ maps.');
      }catch(e){console.error(e);toast('Could not export: '+e.message,true);}
      finally{button.disabled=false;button2.disabled=false;}
    },45);
  }
  function saveSession(){
    if(!available())return;
    const config=sessionData();
    download(new Blob([JSON.stringify(config,null,2)],{type:'application/json'}),ChopExport.cleanName(state.fileName)+'_choproom_session.json');
    toast('Session markers saved. Keep the original audio file to reopen it.');
  }
  async function openSession(file){
    if(!file)return;
    if(!available())return toast('Load your source audio before opening a saved session.',true);
    try{
      const value=JSON.parse(await file.text());
      if(value.app!=='CHOPROOM'||value.version!==1||value.sourceFile!==state.fileName)throw new Error('This session belongs to another audio file. Load its matching source first.');
      if(Math.abs(Number(value.duration)-state.duration)>.1)throw new Error('The source audio duration differs from the saved session.');
      const markers=value.markers,lo=Number(value.regionStart),hi=Number(value.regionEnd);
      if(!Array.isArray(markers)||markers.length<2||markers.length>513||!(lo>=0&&hi<=state.duration&&hi>lo))throw new Error('Invalid chop marker range.');
      if(markers.some((n,i)=>!Number.isFinite(n)||(i>0&&n-markers[i-1]<.02)))throw new Error('Markers must be sorted and non-overlapping.');
      if(Math.abs(markers[0]-lo)>.01||Math.abs(markers[markers.length-1]-hi)>.01)throw new Error('Saved region and chop boundaries do not match.');
      stopAudio();
      state.regionStart=lo;state.regionEnd=hi;
      state.markers=markers.slice();
      restoreCollection(value);
      window.ChopArrangerUI?.restore?.(value.arrangement);
      if(Number.isFinite(Number(value.sensitivity)))$('sensitivity').value=Math.max(0,Math.min(100,Number(value.sensitivity)));
      $('sensitivityText').textContent=$('sensitivity').value+'%';
      if(Number.isFinite(Number(value.manualCutPercent)))$('manualCutPercent').value=Math.max(1,Math.min(99,Number(value.manualCutPercent)));
      $('manualCutText').textContent=$('manualCutPercent').value+'%';
      state.settings={};
      for(const [index,opts] of Object.entries(value.settings||{})){
        const validNumeric=/^\d+$/.test(index)&&Number(index)<state.markers.length-1;
        const validVariant=ChopVariants.isVariantId(index)&&state.variants.some(v=>v.id===index);
        if((!validNumeric&&!validVariant)||!opts||typeof opts!=='object')continue;
        const fade=opts.fadeMs==null?8:Number(opts.fadeMs);
        state.settings[index]={pitch:Math.max(-12,Math.min(12,Number(opts.pitch)||0)),fadeMs:Math.max(0,Math.min(40,Number.isFinite(fade)?fade:8)),reverse:!!opts.reverse};
      }
      $('tempo').value=Math.max(40,Math.min(240,Number(value.tempo)||90));
      state.mode=['grid','transient','manual'].includes(value.mode)?value.mode:'grid';
      // Always reopen in Seek so loading a session never arms the cutting tool.
      state.tool='seek';
      if(Number.isFinite(Number(value.sensitivity))){
        $('sensitivity').value=Math.max(0,Math.min(100,Number(value.sensitivity)));
        $('sensitivityText').textContent=$('sensitivity').value+'%';
      }
      syncModeUI();
      $('detectedCount').textContent=(markers.length-2)+' CUTS · '+(markers.length-1)+' PADS';
      state.selected=0;state.zoom=Math.max(1,Math.min(12,state.duration/(hi-lo)*.9));state.viewStart=lo;clampView();
      $('zoomAmount').textContent=state.zoom.toFixed(1)+'×';
      redrawCollection();
      toast('Restored '+(markers.length-1)+' chops from your saved session.');
      return true;
    }catch(e){toast('Could not restore session: '+e.message,true);return false;}
  }
  function formatSize(bytes){
    if(!bytes)return 'LOCAL FILE';
    return bytes>1e6?(bytes/1e6).toFixed(1)+' MB':(bytes/1e3).toFixed(0)+' KB';
  }
  async function loadAudio(data,name,size=0){
    try{
      const ctx=audioContext();
      stopAudio();
      toast('Reading and drawing waveform…');
      const buffer=await ctx.decodeAudioData(data.slice(0));
      if(!buffer.length||!Number.isFinite(buffer.duration))throw new Error('Empty audio buffer');
      if(buffer.duration>1800)throw new Error('Please choose audio shorter than 30 minutes.');
      state.audio=buffer;state.duration=buffer.duration;state.fileName=name;state.fileSize=size||data.byteLength;
      state.peaks=computePeaks(buffer);state.playTime=0;state.viewStart=0;
      state.tool='seek';
      state.transientCandidates=null;state.transientRegionKey='';
      state.regionStart=0;state.regionEnd=state.mode==='manual'
        ?state.duration:Math.min(state.duration,8*4*60/Number($('tempo').value||90));
      state.zoom=Math.max(1,Math.min(12,state.duration/state.regionEnd*.9));
      state.markers=state.mode==='manual'?[state.regionStart,state.regionEnd]:Array.from({length:17},(_,i)=>state.regionStart+i/16*(state.regionEnd-state.regionStart));
      state.settings={};state.selected=0;state.editingChop=0;state.removed=[];state.variants=[];
      state.banks=[ChopCollection.emptyBank()];
      for(let i=0;i<state.markers.length-1;i++)ChopCollection.assignFirstAvailable(state.banks,i);
      state.activeBank=0;state.padMap=state.banks[0];
      const resumed=restoreAutosave();
      if(!resumed)window.ChopArrangerUI?.restore?.(null);
      $('sourceName').textContent=name;
      $('sourceDescription').textContent=buffer.numberOfChannels+' channel'+(buffer.numberOfChannels>1?'s':'')+' · '+(buffer.sampleRate/1000).toFixed(1)+' kHz · Ready to chop';
      $('railTrack').textContent=name;$('railFormat').textContent='WAV / decoded PCM';
      $('duration').textContent=durationString(state.duration,false);
      $('fileSize').textContent=formatSize(state.fileSize);
      $('waveEmpty').style.display='none';
      $('transportTime').innerHTML='00:00.00 <span>/ '+durationString(state.duration)+'</span>';
      $('zoomAmount').textContent=state.zoom.toFixed(1)+'×';enabledControls();syncModeUI();updateRegionText();updateSelection();renderBanks();renderPads();renderVault();drawWave();
      window.ChopArrangerUI?.sourceChanged?.();
      window.ChopArrangerUI?.autoPopulate?.();
      if(state.mode==='transient')transientChop();
      toast((resumed?'Restored saved cuts for ':'Loaded ')+name+'. '+(state.markers.length-1)+' pad'+(state.markers.length===2?'':'s')+' ready.');
    }catch(err){
      console.error(err);toast('Could not read this audio file: '+err.message,true);
    }
  }
  async function loadFile(file){
    if(!file)return;
    if(file.size>150*1024*1024)return toast('For smooth editing, choose a sample under 150 MB.',true);
    const data=await file.arrayBuffer();
    await loadAudio(data,file.name,file.size);
  }
  async function loadDemo(){
    try{
      const choice=$('libraryTrack');
      if(!choice.value)return toast('Choose any local audio file to begin.',true);
      const resp=await fetch(choice.value,{cache:'no-store'});
      if(!resp.ok)throw new Error('Demo is unavailable');
      const data=await resp.arrayBuffer();
      const title=choice.selectedOptions[0].textContent.trim();
      await loadAudio(data,title+(choice.value.endsWith('.wav')?'.wav':'.mp3'),data.byteLength);
    }catch(err){toast('Choose any local audio file to begin.',true);}
  }
  function savePadOptions(){
    const idx=selectedSlice();
    if(!activeSlice(idx))return;
    state.settings[idx]={pitch:Number($('pitch').value),fadeMs:Number($('fade').value),reverse:$('reversePad').checked};
    updateSelection();persistSession();
  }
  function keyboardHandler(e){
    if(e.altKey||e.metaKey||e.ctrlKey)return;
    if(e.target instanceof HTMLInputElement||e.target instanceof HTMLTextAreaElement||e.target.isContentEditable)return;
    const key=e.key.toLowerCase();
    if(key==='escape'&&state.tool!=='seek'){setTool('seek');return;}
    if(key===' '){e.preventDefault();playSource();return;}
    const index=KEYS.indexOf(key);
    if(index>=0){e.preventDefault();playPad(index);}
  }
  async function midiConnect(){
    if(!navigator.requestMIDIAccess)return toast('MIDI is available in Chrome on localhost. Open this dashboard in Chrome.',true);
    try{
      state.midi=await navigator.requestMIDIAccess({sysex:false});
      state.midiInputs=[...state.midi.inputs.values()];
      window.ChopDevices?.setMidiAccess?.(state.midi);
      if(!state.midiInputs.length){$('midiStatus').textContent='No MIDI controller detected. Connect your FLX4 and retry.';return;}
      for(const input of state.midiInputs)input.onmidimessage=midiMessage;
      const device=state.midiInputs.map(i=>i.name).join(', ');
      $('midiConnect').textContent='✓ MIDI connected';
      $('midiStatus').textContent='Connected: '+device+'. Click this button again, choose a pad, then press its controller button to learn it.';
      toast('MIDI connected: '+device);
      state.midiLearning=-1;
    }catch(e){toast('MIDI access denied or unavailable: '+e.message,true);}
  }
  function midiMessage(e){
    // Do not steal notes from a different connected controller or DJ mode.
    // A selected profile matches only its selected/identified MIDI input.
    if(window.ChopDevices&&!window.ChopDevices.shouldHandleInput(e.currentTarget||e.target))return;
    const data=e.data,status=data[0]&0xf0,num=data[1],velocity=data[2];
    if(!((status===0x90&&velocity>0)||(status===0xb0&&velocity>0)))return;
    const key=status.toString(16)+':'+num;
    const profileKey=(window.ChopDevices?.selectedProfile||'generic')+'|'+key;
    if(state.midiLearning>=0){
      state.midiMap[profileKey]=state.midiLearning;
      localStorage.setItem('choproom-midi-v1',JSON.stringify(state.midiMap));
      const pad=state.midiLearning;state.midiLearning=-1;
      $('midiStatus').textContent='Mapped '+key+' to pad '+String(pad+1).padStart(2,'0')+'. Click MIDI controller to learn more pads.';
      toast('MIDI mapping saved for pad '+(pad+1));return;
    }
    const match=state.midiMap[profileKey]??state.midiMap[key];
    let pad=null;
    if(Number.isInteger(match))pad=match;
    else if(status===0x90&&num>=36&&num<=51)pad=num-36;
    if(Number.isInteger(pad)){
      playPad(pad);
      window.ChopDevices?.markTriggered?.(pad);
    }
  }
  function toggleLearn(){
    if(!state.midi){midiConnect();return;}
    state.midiLearning=state.midiLearning===-1?0:-1;
    $('midiStatus').textContent=state.midiLearning===-1?'MIDI learn finished. Your mappings are saved.':'MIDI learn: click a browser pad, then press its matching controller pad.';
  }
  function bind(){
    $('chooseFile').addEventListener('click',e=>{e.stopPropagation();$('fileInput').click();});
    $('fileInput').addEventListener('change',e=>loadFile(e.target.files[0]));
    const zone=$('dropzone');
    zone.addEventListener('click',e=>{if(e.target.closest('button'))return;$('fileInput').click();});
    zone.addEventListener('keydown',e=>{if(e.key==='Enter')$('fileInput').click();});
    for(const el of [zone,document.body]){
      el.addEventListener('dragover',e=>{if(e.dataTransfer&&e.dataTransfer.types.includes('Files')){e.preventDefault();zone.classList.add('dragging');}});
      el.addEventListener('dragleave',()=>zone.classList.remove('dragging'));
      el.addEventListener('drop',e=>{if(!e.dataTransfer||!e.dataTransfer.files.length)return;e.preventDefault();zone.classList.remove('dragging');loadFile(e.dataTransfer.files[0]);});
    }
    $('loadDemo').addEventListener('click',loadDemo);
    $('playSource').addEventListener('click',playSource);
    $('playSelected').addEventListener('click',playSelected);
    $('addAtPlayhead').addEventListener('click',()=>{
      if(state.tool==='slice')addMarker(state.playTime);
    });
    $('deleteCut').addEventListener('click',removeMarker);
    $('zoomIn').addEventListener('click',()=>doZoom(1.5));
    $('zoomOut').addEventListener('click',()=>doZoom(1/1.5));
    $('fitWave').addEventListener('click',()=>{state.zoom=1;state.viewStart=0;$('zoomAmount').textContent='1.0×';drawWave();});
    document.querySelectorAll('[data-mode]').forEach(el=>el.addEventListener('click',()=>setMode(el.dataset.mode)));
    document.querySelectorAll('[data-count]').forEach(el=>el.addEventListener('click',()=>{
      state.gridCount=Number(el.dataset.count);
      document.querySelectorAll('[data-count]').forEach(x=>x.classList.toggle('active',x===el));
    }));
    $('sliceNow').addEventListener('click',gridChop);
    $('clearCuts').addEventListener('click',()=>{
      if(state.mode==='manual'){state.regionStart=0;state.regionEnd=state.duration;}
      changeMarkers([]);
    });
    $('seekTool').addEventListener('click',()=>setTool('seek'));
    $('slicerTool').addEventListener('click',()=>setTool(state.tool==='slice'?'seek':'slice'));
    $('deleteTool').addEventListener('click',()=>setTool(state.tool==='delete'?'seek':'delete'));
    $('addBank').addEventListener('click',createBank);
    $('vaultNewBank').addEventListener('click',createBank);
    $('vaultTargetBank').addEventListener('change',e=>{
      state.targetBank=Number(e.target.value)||0;
      renderVault();
    });
    $('vaultTargetPad').addEventListener('change',e=>{
      state.targetPad=Math.max(0,Math.min(15,Number(e.target.value)||0));
      renderVault();
    });
    $('showOnTrack').addEventListener('click',()=>{
      if(!activeSlice(state.editingChop))return toast('Select an active chop to locate it in the waveform.',true);
      focusChop(state.editingChop,true);
    });
    $('followPad').addEventListener('change',e=>{
      if(e.target.checked&&activeSlice(state.editingChop))focusChop(state.editingChop);
    });
    $('clearPad').addEventListener('click',()=>{
      const index=state.padMap[state.selected];state.padMap[state.selected]=null;
      redrawCollection();
      toast(index==null?'Pad is already empty.':'Pad cleared. '+chopLabel(index)+' remains in the library.');
    });
    $('duplicateChop').addEventListener('click',()=>duplicateChop());
    $('splitChop').addEventListener('click',()=>splitChop());
    $('splitAtPlayhead').addEventListener('click',()=>splitChop(selectedSlice(),state.playTime));
    $('makeHalfChop').addEventListener('click',keepFirstHalfOfCopy);
    $('toggleOrganizer').addEventListener('click',()=>{
      const wrap=$('organizerBanks').closest('.organizer-wrap');
      const collapsed=wrap.dataset.collapsed==='true';
      wrap.dataset.collapsed=collapsed?'false':'true';
      $('toggleOrganizer').textContent=collapsed?'Hide organizer':'Show organizer';
      $('toggleOrganizer').setAttribute('aria-expanded',String(collapsed));
      renderOrganizer();
    });
    $('showRemoved').addEventListener('change',renderVault);
    $('focus4').addEventListener('click',()=>focusBars(4));
    $('focus8').addEventListener('click',()=>focusBars(8));
    $('fullRange').addEventListener('click',()=>focusBars(0));
    $('sensitivity').addEventListener('input',()=>{
      $('sensitivityText').textContent=$('sensitivity').value+'%';
      if(state.mode==='transient'){
        state.pauseAutoSync=true;
        try{transientChop();}finally{state.pauseAutoSync=false;}
      }
    });
    $('sensitivity').addEventListener('change',()=>{if(state.mode==='transient')window.ChopArrangerUI?.autoPopulate?.();});
    $('manualCutPercent').addEventListener('input',previewManualCut);
    $('placePercentCut').addEventListener('click',()=>{
      if(state.mode!=='manual')return;
      const at=manualCutLocation();
      if(at!=null)addMarker(at);
    });
    $('libraryTrack').addEventListener('change',loadDemo);
    $('startAt').addEventListener('change',()=>updateSliceBoundary('start'));
    $('endAt').addEventListener('change',()=>updateSliceBoundary('end'));
    for(const id of ['pitch','fade','reversePad'])$(id).addEventListener('input',savePadOptions);
    $('resetPad').addEventListener('click',resetChopSettings);
    $('mono').addEventListener('change',e=>{state.mono=e.target.checked;});
    $('midiConnect').addEventListener('click',toggleLearn);
    $('exportAll').addEventListener('click',exportKit);
    $('exportBottom').addEventListener('click',exportKit);
    $('saveProject').addEventListener('click',saveSession);
    $('openProject').addEventListener('click',()=>$('sessionInput').click());
    $('sessionInput').addEventListener('change',e=>openSession(e.target.files[0]));
    window.addEventListener('keydown',keyboardHandler);
    window.addEventListener('resize',drawWave);
    wave.addEventListener('pointerdown',e=>{
      if(!available()||e.button!==0||e.ctrlKey)return;
      const r=wave.getBoundingClientRect(),x=e.clientX-r.left;
      const closest=state.tool==='delete'?-1:ChopMarkers.nearestHandle(state.markers,state.viewStart,visibleSpan(),r.width,x);
      // Drag handles respond only after pointer movement. Simply clicking on
      // a handle continues to seek, preserving the audition workflow.
      if(closest>=0){
        state.dragged=-1;
        state.pointer={id:e.pointerId,index:closest,x,moved:false};
        wave.setPointerCapture(e.pointerId);
        return;
      }
      state.dragged=-1;state.pointer=null;
      const time=xToView(x,r.width);
      if(state.tool==='slice')addMarker(time);
      else if(state.tool==='delete')deleteAtTime(time);
      else{
        seekTo(time);
        chooseChop(chopAtTime(time));
      }
    });
    wave.addEventListener('pointermove',e=>{
      if(!available())return;
      const r=wave.getBoundingClientRect(),x=e.clientX-r.left;
      if(!state.pointer||state.pointer.id!==e.pointerId){
        const closest=state.tool==='delete'?-1:ChopMarkers.nearestHandle(state.markers,state.viewStart,visibleSpan(),r.width,x);
        wave.style.cursor=closest>=0?'ew-resize':state.tool==='slice'?'crosshair':state.tool==='delete'?'cell':'pointer';
        return;
      }
      if(!state.pointer.moved&&Math.abs(x-state.pointer.x)<5)return;
      state.pointer.moved=true;
      state.dragged=state.pointer.index;
      wave.style.cursor='ew-resize';
      state.editingChop=Math.max(0,Math.min(state.markers.length-2,state.dragged===0?0:state.dragged-1));
      moveMarkerTo(state.dragged,xToView(x,r.width));
    });
    function releasePointer(e){
      if(state.pointer&&e.pointerId===state.pointer.id){
        const {moved}=state.pointer;
        if(moved){
          renderPads();updateSelection();persistSession();
        }else if(e.type==='pointerup'){
          const r=wave.getBoundingClientRect();
          const time=xToView(e.clientX-r.left,r.width);
          if(state.tool==='slice')addMarker(time);
          else if(state.tool==='delete')deleteAtTime(time);
          else {
            seekTo(time);
            chooseChop(chopAtTime(time));
          }
        }
        if(wave.hasPointerCapture(e.pointerId))wave.releasePointerCapture(e.pointerId);
      }
      state.dragged=-1;state.pointer=null;
      wave.style.cursor=state.tool==='slice'?'crosshair':state.tool==='delete'?'cell':'pointer';
    }
    wave.addEventListener('pointerup',releasePointer);
    wave.addEventListener('pointercancel',releasePointer);
    overview.addEventListener('click',e=>{
      if(!available())return;
      const r=overview.getBoundingClientRect(),t=(e.clientX-r.left)/r.width*state.duration;
      state.viewStart=t-visibleSpan()/2;clampView();
      seekTo(t);
    });
    function trackWheel(e){
      if(!available())return;
      if(e.ctrlKey||e.metaKey||e.altKey){
        if(e.currentTarget===wave){
          e.preventDefault();
          doZoom(e.deltaY<0?1.22:1/1.22);
        }
        return;
      }
      // macOS trackpads report a horizontal two-finger gesture as deltaX.
      // Keep vertical two-finger scrolling available for the whole page.
      const horizontal=Math.abs(e.deltaX)>0.8?e.deltaX:e.shiftKey?e.deltaY:0;
      if(Math.abs(horizontal)<0.8||state.zoom<=1)return;
      if(!e.shiftKey&&Math.abs(e.deltaX)<Math.abs(e.deltaY)*.3)return;
      const width=Math.max(1,wave.getBoundingClientRect().width);
      const mode=e.deltaMode===1?16:e.deltaMode===2?width:1;
      state.viewStart=ChopTrackView.panScroll(state.viewStart,visibleSpan(),state.duration,horizontal*mode,width);
      e.preventDefault();
      drawWave();
    }
    for(const canvas of [wave,overview,$('padFocusWave'),$('padTrackOverview')]){
      canvas.addEventListener('wheel',trackWheel,{passive:false});
    }
    function miniGeometry(kind,canvas){
      const index=selectedSlice();
      if(!activeSlice(index))return null;
      const range=chopRange(index);
      const start=range.start,end=range.end;
      const detail=ChopTrackView.previewWindow(start,end,state.duration);
      const mapping=kind==='detail'?detail:{from:0,span:state.duration};
      const rect=canvas.getBoundingClientRect();
      return {index,start,end,detail,mapping,width:rect.width,rect};
    }
    function miniPointerDown(e,kind){
      if(!available()||e.button!==0)return;
      const canvas=e.currentTarget,geo=miniGeometry(kind,canvas);
      if(!geo)return;
      const x=e.clientX-geo.rect.left;
      const side=ChopTrackView.nearestSide(geo.start,geo.end,geo.mapping.from,geo.mapping.span,geo.width,x,kind==='detail'?18:11);
      if(!side){
        if(kind==='overview'){
          const time=ChopTrackView.timeAtX(x,geo.width,0,state.duration);
          state.viewStart=time-visibleSpan()/2;clampView();drawWave();
        }else{
          const time=ChopTrackView.timeAtX(x,geo.width,geo.mapping.from,geo.mapping.span);
          seekTo(time);
        }
        return;
      }
      state.miniDrag={
        id:e.pointerId,canvas,kind,side,
        index:geo.index,markerIndex:typeof geo.index==='number'?geo.index+(side==='end'?1:0):null,
        detailPreview:geo.detail,mapping:geo.mapping,width:geo.width,initialX:x,moved:false
      };
      e.preventDefault();
      canvas.setPointerCapture(e.pointerId);
      canvas.style.cursor='ew-resize';
    }
    function miniPointerMove(e,kind){
      const canvas=e.currentTarget,gesture=state.miniDrag;
      if(gesture&&gesture.canvas===canvas&&gesture.id===e.pointerId){
        const x=e.clientX-canvas.getBoundingClientRect().left;
        if(!gesture.moved&&Math.abs(x-gesture.initialX)<3)return;
        if(!gesture.moved&&state.playing)stopAudio();
        gesture.moved=true;
        moveChopBoundary(gesture.index,gesture.side,ChopTrackView.timeAtX(x,gesture.width,gesture.mapping.from,gesture.mapping.span));
        return;
      }
      const geo=miniGeometry(kind,canvas);
      if(!geo)return;
      const x=e.clientX-geo.rect.left;
      const side=ChopTrackView.nearestSide(geo.start,geo.end,geo.mapping.from,geo.mapping.span,geo.width,x,kind==='detail'?18:11);
      canvas.style.cursor=side?'ew-resize':'crosshair';
    }
    function miniPointerEnd(e){
      const gesture=state.miniDrag;
      if(!gesture||gesture.canvas!==e.currentTarget||gesture.id!==e.pointerId)return;
      if(e.currentTarget.hasPointerCapture(e.pointerId))e.currentTarget.releasePointerCapture(e.pointerId);
      state.miniDrag=null;
      e.currentTarget.style.cursor='crosshair';
      if(gesture.moved){
        renderPads();renderVault();persistSession();
      }
      drawWave();
    }
    for(const [canvas,kind] of [[$('padFocusWave'),'detail'],[$('padTrackOverview'),'overview']]){
      canvas.addEventListener('pointerdown',e=>miniPointerDown(e,kind));
      canvas.addEventListener('pointermove',e=>miniPointerMove(e,kind));
      canvas.addEventListener('pointerup',miniPointerEnd);
      canvas.addEventListener('pointercancel',miniPointerEnd);
    }
    $('tempo').addEventListener('input',drawWave);
  }
  if(window.ChopArrangerUI){
    window.ChopArrangerUI.mount({
      getAudio:()=>state.audio,
      getMarkers:()=>state.markers,
      getVariants:()=>state.variants,
      getBanks:()=>state.banks,
      getActiveBank:()=>state.activeBank,
      getChopRefs:()=>allChopRefs().filter(ref=>activeSlice(ref)),
      getPeaks:()=>state.peaks,
      getTempo:()=>Number($('tempo').value)||90,
      getSelectedChop:()=>activeSlice(state.editingChop)?state.editingChop:null,
      onBeforePlay:()=>stopAudio(),
      onChange:()=>persistSession(),
    });
  }
  if(window.ChopDevices){
    window.ChopDevices.mount({
      onConnectMIDI:()=>midiConnect(),
      onSelectPad:pad=>selectPad(pad),
      onPreviewPad:pad=>playPad(pad),
      onAssignChop:pad=>{
        if(!activeSlice(state.editingChop))return false;
        assignChop(state.editingChop,pad,state.activeBank);
        return true;
      },
      getSelectedPad:()=>state.selected,
      getPadSource:pad=>{
        const ref=state.padMap[pad];
        return activeSlice(ref)?chopLabel(ref):null;
      },
      getBankName:()=> 'Bank '+bankName(state.activeBank),
    });
  }
  document.addEventListener('choproom:themechange',()=>{
    renderPads();drawWave();
    window.ChopArrangerUI?.sourceChanged?.();
  });
  // Supply a fresh read-only export snapshot, including the decoded audio.
  window.ChoproomExportState=()=>({
    audio:state.audio,markers:state.markers.slice(),
    variants:state.variants.map(v=>({...v})),
    removed:state.removed.slice(),settings:{...state.settings},
    name:state.fileName,bpm:Number($('tempo').value)||90,
  });
  bind();enabledControls();renderBanks();renderPads();renderVault();drawWave();
  // Show an example from the user's existing local DJ library with zero upload or playback.
  loadDemo();
})();
