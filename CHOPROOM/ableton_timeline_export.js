/* Exports active CHOPROOM slices as individually movable clips on ONE Ableton audio track.
 * The static Live 12 template contains no imported user audio or clips.
 * All generation takes place locally in the browser; originals are not edited.
 */
(function(root){
  'use strict';
  const GAP_SECONDS=.20;
  const $=id=>document.getElementById(id);
  const limited=(n,a,b,f)=>Number.isFinite(Number(n))?Math.max(a,Math.min(b,Number(n))):f;
  const textValue=(node,tag,value)=>{
    const child=node.querySelector(tag);
    if(!child)throw new Error('Ableton template is missing '+tag);
    child.setAttribute('Value',String(value));
  };
  const xmlParser=(str)=>{
    const doc=new DOMParser().parseFromString(str,'application/xml');
    if(doc.querySelector('parsererror'))throw new Error('Invalid Ableton template XML');
    return doc;
  };
  const formatted=n=>Number(Number(n).toPrecision(13)).toString();
  const download=(blob,name)=>{
    const link=document.createElement('a');
    const url=URL.createObjectURL(blob);
    link.href=url;link.download=name;
    document.body.append(link);link.click();link.remove();
    setTimeout(()=>URL.revokeObjectURL(url),60000);
  };
  const indexName=(i)=>String(i+1).padStart(3,'0');
  function activeSlices(session){
    const {markers,variants=[],removed=[]}=session;
    if(!Array.isArray(markers)||markers.length<2)throw new Error('Load a song and cut some chops first.');
    const refs=[];
    for(let i=0;i<markers.length-1;i++){
      if(removed.includes(i))continue;
      const a=Number(markers[i]),b=Number(markers[i+1]);
      if(Number.isFinite(a)&&Number.isFinite(b)&&b-a>=.005)
        refs.push({ref:i,start:a,end:b,label:'Chop '+(i+1)});
    }
    for(const v of variants){
      if(!v||removed.includes(v.id)||!Number.isFinite(v.start)||!Number.isFinite(v.end)||v.end-v.start<.005)continue;
      refs.push({ref:v.id,start:v.start,end:v.end,label:v.label||'Copy '+v.id});
    }
    // Chronological source order keeps every original chop and independent copy
    // easy to locate in one row, with stable tie order for overlapping copies.
    refs.sort((a,b)=>a.start-b.start||a.end-b.end||String(a.ref).localeCompare(String(b.ref)));
    return refs;
  }
  async function readTemplate(name){
    const res=await fetch('./'+name,{cache:'no-store'});
    if(!res.ok)throw new Error('Could not load '+name);
    return res.text();
  }
  async function gzip(bytes){
    if(typeof CompressionStream!=='function')throw new Error('This browser cannot create Ableton sets. Use current Chrome or Safari.');
    const stream=new Blob([bytes]).stream().pipeThrough(new CompressionStream('gzip'));
    return new Uint8Array(await new Response(stream).arrayBuffer());
  }
  async function build(session,{gapSeconds=GAP_SECONDS}={}){
    const audio=session.audio;
    if(!audio?.getChannelData||!root.ChopExport)throw new Error('Load a song into CHOPROOM first.');
    const slices=activeSlices(session);
    if(!slices.length)throw new Error('There are no active chops to export.');
    if(slices.length>510)throw new Error('Too many chops for one Ableton export.');
    const bpm=limited(session.bpm,30,300,90),gap=limited(gapSeconds,.01,5,GAP_SECONDS);
    const [setXML,clipXML]=await Promise.all([
      readTemplate('ableton_one_track_template.xml'),
      readTemplate('ableton_clip_template.xml'),
    ]);
    const doc=xmlParser(setXML),singleClip=xmlParser(clipXML);
    const clips=doc.querySelector('LiveSet > Tracks > AudioTrack > DeviceChain > MainSequencer > Sample > ArrangerAutomation > Events');
    if(!clips||clips.children.length)throw new Error('Ableton timeline template is not empty.');
    const track=doc.querySelector('LiveSet > Tracks > AudioTrack');
    textValue(track,'Name > EffectiveName','CHOPROOM · ALL CHOPS');
    textValue(track,'Name > UserName','CHOPROOM · ALL CHOPS');
    const entries=[],metadata=[];
    let seconds=0;
    const sampleRate=audio.sampleRate;
    const sourceName=root.ChopExport.cleanName(session.name||'sample');
    for(let i=0;i<slices.length;i++){
      const slice=slices[i];
      const wav=root.ChopExport.wav16(audio,slice.start,slice.end,session.settings?.[slice.ref]||{});
      const channels=Math.min(2,audio.numberOfChannels);
      const frames=(wav.length-44)/(2*channels),duration=frames/sampleRate;
      if(!Number.isFinite(duration)||duration<=0)throw new Error('Invalid chop duration');
      const path='Samples/Imported/'+indexName(i)+'_'+root.ChopExport.cleanName(slice.label)+'.wav';
      const clip=doc.importNode(singleClip.documentElement,true);
      const startBeat=seconds*bpm/60,clipBeats=duration*bpm/60;
      const endBeat=startBeat+clipBeats;
      clip.setAttribute('Id',String(i));
      clip.setAttribute('Time',formatted(startBeat));
      textValue(clip,'CurrentStart',formatted(startBeat));
      textValue(clip,'CurrentEnd',formatted(endBeat));
      textValue(clip,'Name',slice.label);
      textValue(clip,'TakeId',String(i+1));
      textValue(clip,'IsWarped','true');
      for(const key of ['LoopStart','StartRelative','HiddenLoopStart'])textValue(clip,'Loop > '+key,'0');
      for(const key of ['LoopEnd','OutMarker','HiddenLoopEnd'])textValue(clip,'Loop > '+key,formatted(clipBeats));
      textValue(clip,'Loop > LoopOn','true');
      textValue(clip,'SampleRef > FileRef > RelativePathType','3');
      textValue(clip,'SampleRef > FileRef > RelativePath',path);
      textValue(clip,'SampleRef > FileRef > Path','');
      textValue(clip,'SampleRef > FileRef > OriginalFileSize',wav.length);
      textValue(clip,'SampleRef > FileRef > OriginalCrc','0');
      textValue(clip,'SampleRef > LastModDate','0');
      textValue(clip,'SampleRef > DefaultDuration',frames);
      textValue(clip,'SampleRef > DefaultSampleRate',sampleRate);
      const marks=clip.querySelector('WarpMarkers');
      if(!marks)throw new Error('Missing warp marker template');
      marks.replaceChildren();
      for(const [id,sec,beat] of [[0,0,0],[1,duration,clipBeats]]){
        const mark=doc.createElement('WarpMarker');
        mark.setAttribute('Id',String(id));
        mark.setAttribute('SecTime',formatted(sec));
        mark.setAttribute('BeatTime',formatted(beat));
        marks.append(mark);
      }
      clips.append(clip);
      entries.push({name:path,data:wav});
      metadata.push({number:i+1,label:slice.label,originalRef:slice.ref,file:path,
        arrangementSeconds:seconds,durationSeconds:duration,gapAfterSeconds:i===slices.length-1?0:gap,
        sourceStartSeconds:slice.start,sourceEndSeconds:slice.end});
      seconds+=duration+(i<slices.length-1?gap:0);
    }
    // Keep transport at the beginning of the exported sequence.
    const current=doc.querySelector('LiveSet > Transport > CurrentTime');
    if(current)current.setAttribute('Value','0');
    // Live 12 stores the master tempo in the Main Track mixer control.
    const tempo=doc.querySelector('LiveSet > MainTrack > DeviceChain > Mixer > Tempo > Manual');
    if(tempo)tempo.setAttribute('Value',String(bpm));
    // DOMParser preserves the XML declaration from the template in some
    // browsers. Avoid writing two declarations into a .als archive.
    const resultXML=new XMLSerializer().serializeToString(doc)
      .replace(/^\s*<\?xml[^>]*\?>\s*/i,'');
    const resultALS=await gzip(new TextEncoder().encode('<?xml version="1.0" encoding="UTF-8"?>\n'+resultXML));
    const project='CHOPROOM_'+sourceName+'_All_Chops';
    entries.unshift({name:project+'.als',data:resultALS});
    entries.push({name:'chop_timeline.json',data:JSON.stringify({source:session.name,bpm,gapSeconds:gap,trackName:'CHOPROOM · ALL CHOPS',totalDurationSeconds:seconds,clips:metadata},null,2)+'\n'});
    entries.push({name:'OPEN_IN_ABLETON.txt',data:[
      'CHOPROOM · ABLETON LIVE ONE-TRACK CHOP ARRANGEMENT','',
      '1. Extract the entire ZIP, preserving the Samples/Imported folder.',
      '2. Open '+project+'.als in Ableton Live 12.',
      '3. In Arrangement View, use the audio track CHOPROOM · ALL CHOPS.',
      '4. Every active chop is a separate movable audio clip, ordered by source start.',
      '5. Each clip is followed by '+gap.toFixed(3)+' seconds of blank timeline space, except the last clip.',
      '6. Move, delete, duplicate, or extend clips freely in Ableton.',
      '',
      'All slice processing is rendered into WAV files: pitch, reverse and edge fades.',
      'The session contains one audio row. No source songs or user sessions were changed.',
      'Keep the exported .als and Samples/Imported folder together when moving the project.',
      'The Ableton set is generated from an installed Live 12 file layout.'
    ].join('\n')});
    return {blob:root.ChopExport.zipStore(entries),count:metadata.length,seconds,gap,metadata};
  }
  function mount(){
    const anchor=document.querySelector('.export-card');
    if(!anchor||!root.ChoproomExportState)return false;
    if($('exportAbletonTimeline'))return true;
    const wrap=document.createElement('div');
    wrap.className='ableton-timeline-export';
    wrap.style.cssText='display:flex;flex-wrap:wrap;align-items:center;justify-content:flex-end;gap:10px;margin-top:14px';
    const label=document.createElement('label');
    label.style.cssText='display:flex;align-items:center;gap:6px;font-size:12px';
    label.textContent='Gap between chops';
    const input=document.createElement('input');input.type='number';input.id='abletonChopGap';
    input.min='0.01';input.max='5';input.step='0.05';input.value=String(GAP_SECONDS);
    input.style.cssText='width:70px;padding:8px;border-radius:7px';
    input.setAttribute('aria-label','Silence gap in seconds between exported Ableton clips');
    label.append(input,document.createTextNode('sec'));
    const button=document.createElement('button');button.id='exportAbletonTimeline';
    button.className='button primary large';button.type='button';
    button.textContent='Export Ableton timeline (.als)';
    const status=document.createElement('span');status.id='exportAbletonStatus';
    status.style.cssText='font-size:12px;opacity:.85';status.setAttribute('role','status');
    button.onclick=async()=>{
      button.disabled=true;button.textContent='Building Ableton session…';status.textContent='';
      try{
        const s=root.ChoproomExportState(),res=await build(s,{gapSeconds:input.value});
        const stem=root.ChopExport.cleanName(s.name.replace(/\.[^.]+$/,''));
        download(res.blob,stem+'_CHOPROOM_Ableton_Timeline.zip');
        status.textContent=res.count+' separate clips · 1 audio track · '+res.gap.toFixed(2)+'s gaps';
      }catch(err){console.error(err);status.textContent='Export failed: '+(err?.message||err);}
      finally{button.disabled=false;button.textContent='Export Ableton timeline (.als)';}
    };
    wrap.append(label,button,status);anchor.after(wrap);
    return true;
  }
  root.ChoproomAbletonTimeline={build,activeSlices,mount};
  if(document.readyState==='loading')document.addEventListener('DOMContentLoaded',mount,{once:true});else mount();
})(typeof window!=='undefined'?window:globalThis);
