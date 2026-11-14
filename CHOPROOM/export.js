/* Self-contained WAV, MIDI, SFZ and ZIP encoders. Works offline in the browser. */
(function (root) {
  'use strict';
  const encoder = new TextEncoder();
  function u16(view, offset, n) { view.setUint16(offset, n >>> 0, true); }
  function u32(view, offset, n) { view.setUint32(offset, n >>> 0, true); }
  function cleanName(value) { return String(value || 'chop').normalize('NFKD').replace(/[^a-zA-Z0-9 _-]/g, '').trim().replace(/\s+/g, '_').slice(0, 42) || 'chop'; }
  function midiNoteName(note) { return ['C','C#','D','D#','E','F','F#','G','G#','A','A#','B'][note % 12] + (Math.floor(note / 12) - 2); } // Ableton: MIDI 60 = C3

  function wav16(audio, start, end, opts) {
    if (!audio || typeof audio.getChannelData !== 'function') throw new Error('Audio buffer required');
    const rate = audio.sampleRate, channels = Math.min(2, audio.numberOfChannels);
    const first = Math.max(0, Math.floor(start * rate));
    const last = Math.min(audio.length, Math.ceil(end * rate));
    const semitones = Number(opts && opts.pitch) || 0;
    const speed = Math.pow(2, semitones / 12);
    const count = Math.max(1, Math.floor((last - first) / speed));
    const bytes = new ArrayBuffer(44 + count * channels * 2);
    const out = new DataView(bytes), head = new Uint8Array(bytes);
    function str(pos, text) { for (let i=0; i<text.length; i++) head[pos+i] = text.charCodeAt(i); }
    str(0, 'RIFF'); u32(out, 4, bytes.byteLength - 8); str(8, 'WAVE'); str(12,'fmt ');
    u32(out, 16,16); u16(out,20,1);u16(out,22,channels);u32(out,24,rate);
    u32(out,28,rate*channels*2);u16(out,32,channels*2);u16(out,34,16);
    str(36,'data');u32(out,40,count*channels*2);
    const source = Array.from({length:channels},(_,c)=>audio.getChannelData(c));
    const fadeFrames = Math.min(Math.round(((opts && opts.fadeMs) == null ? 8 : opts.fadeMs) * .001 * rate), Math.floor(count/2));
    const reverse = Boolean(opts && opts.reverse);
    for(let i=0;i<count;i++){
      const inputPosition = reverse ? (last - 1 - i*speed) : (first + i*speed);
      const low = Math.max(first,Math.min(last-1,Math.floor(inputPosition)));
      const high = Math.min(last-1,low+1);
      const frac = Math.max(0,Math.min(1,inputPosition-low));
      let envelope=1;
      if(fadeFrames>0) envelope=Math.min(1,i/fadeFrames,(count-1-i)/fadeFrames);
      for(let c=0;c<channels;c++){
        const s=source[c], value=(s[low]*(1-frac)+s[high]*frac)*envelope;
        const q=Math.round(Math.max(-1,Math.min(1,value))*32767);
        out.setInt16(44+(i*channels+c)*2,q,true);
      }
    }
    return new Uint8Array(bytes);
  }

  const crcTable = Array.from({length:256},(_,n)=>{
    for(let k=0;k<8;k++) n=(n&1)?(0xedb88320^(n>>>1)):(n>>>1);
    return n>>>0;
  });
  function crc32(data){
    let sum=0xffffffff;
    for(let i=0;i<data.length;i++)sum=(sum>>>8)^crcTable[(sum^data[i])&255];
    return (sum^0xffffffff)>>>0;
  }
  function zipStore(entries){
    if(!Array.isArray(entries)||entries.length===0)throw new Error('ZIP requires entries');
    if(entries.length>65535)throw new Error('Too many ZIP entries');
    let offset=0;
    const chunks=[],central=[];
    for(const item of entries){
      const name=encoder.encode(String(item.name));
      const data=item.data instanceof Uint8Array ? item.data : encoder.encode(String(item.data||''));
      const crc=crc32(data);
      const local=new Uint8Array(30+name.length),d=new DataView(local.buffer);
      u32(d,0,0x04034b50);u16(d,4,20);u16(d,6,0x800);u16(d,8,0);
      u32(d,14,crc);u32(d,18,data.byteLength);u32(d,22,data.byteLength);u16(d,26,name.length);u16(d,28,0);
      local.set(name,30);
      chunks.push(local,data);
      const centralHead=new Uint8Array(46+name.length),cd=new DataView(centralHead.buffer);
      u32(cd,0,0x02014b50);u16(cd,4,20);u16(cd,6,20);u16(cd,8,0x800);u16(cd,10,0);
      u32(cd,16,crc);u32(cd,20,data.byteLength);u32(cd,24,data.byteLength);u16(cd,28,name.length);
      u32(cd,42,offset);centralHead.set(name,46);central.push(centralHead);
      offset+=local.byteLength+data.byteLength;
      if(offset>0xffffffff)throw new Error('ZIP would exceed 4 GB');
    }
    const centralSize=central.reduce((n,c)=>n+c.byteLength,0);
    const end=new Uint8Array(22),view=new DataView(end.buffer);
    u32(view,0,0x06054b50);u16(view,8,entries.length);u16(view,10,entries.length);
    u32(view,12,centralSize);u32(view,16,offset);
    return new Blob([...chunks,...central,end],{type:'application/zip'});
  }

  function midiVLQ(n){
    n=Math.max(0,Math.floor(n));
    const arr=[n&0x7f];
    while((n>>>=7)>0)arr.unshift((n&0x7f)|0x80);
    return arr;
  }
  function midiFile(events,bpm) {
    bpm=Math.max(40,Math.min(240,Number(bpm)||90));
    // A reference performance, or a one-note-per-pad guide when nothing was recorded.
    const notes=(Array.isArray(events)&&events.length) ? events : Array.from({length:16},(_,i)=>({pad:i,ms:i*(60000/bpm),durationMs:Math.min(300,50000/bpm),velocity:100}));
    const ppq=480,tempo=Math.round(60000000/bpm);
    const tickForMs=(ms)=>Math.max(0,Math.round((ms/1000)*(1000000/tempo)*ppq));
    const items=[{tick:0,order:0,bytes:[0xff,0x51,0x03,(tempo>>16)&255,(tempo>>8)&255,tempo&255]}];
    for(const note of notes){
      const key=36+Math.max(0,Math.min(15,Math.floor(note.pad)));
      const tick=tickForMs(Number(note.ms)||0);
      items.push({tick,order:2,bytes:[0x90,key,Math.max(1,Math.min(127,Math.floor(note.velocity||100)))]});
      items.push({tick:tick+Math.max(30,tickForMs(Number(note.durationMs)||200)),order:1,bytes:[0x80,key,0]});
    }
    items.sort((a,b)=>a.tick-b.tick||a.order-b.order);
    const track=[];let previous=0;
    for(const item of items){track.push(...midiVLQ(item.tick-previous),...item.bytes);previous=item.tick;}
    track.push(0,0xff,0x2f,0);
    const bytes=new Uint8Array(22+track.length),d=new DataView(bytes.buffer);
    bytes.set(encoder.encode('MThd'),0);d.setUint32(4,6);d.setUint16(8,0);d.setUint16(10,1);d.setUint16(12,ppq);
    bytes.set(encoder.encode('MTrk'),14);d.setUint32(18,track.length);bytes.set(track,22);
    return bytes;
  }

  function makeKit(audio,markers,padMap,settings,options) {
    if(!audio||!markers||markers.length<2)throw new Error('Load a sample before exporting');
    const count=Math.min(16,padMap.length),entries=[];
    const stem=cleanName(options.name || 'sample');
    const regions=[];
    let sfz='// CHOPROOM sample map. MIDI notes 36–51 (Ableton C1 to D#2).\n';
    for(let pad=0;pad<count;pad++){
      const index=padMap[pad];
      if(!Number.isInteger(index)||index>=markers.length-1||index<0)continue;
      const opts=settings[index]||{};
      const start=markers[index],end=markers[index+1];
      if(end-start<.005)continue;
      const file=String(pad+1).padStart(2,'0')+'_'+stem+'_chop_'+String(index+1).padStart(2,'0')+'.wav';
      const path='slices/'+file;
      entries.push({name:path,data:wav16(audio,start,end,opts)});
      regions.push({pad:pad+1,sourceChop:index+1,midi:36+pad,note:midiNoteName(36+pad),startSeconds:start,endSeconds:end,pitchSemitones:opts.pitch||0,reversed:Boolean(opts.reverse),file:path});
      sfz+='<region> sample='+path+' key='+(36+pad)+' group=1 off_by=1\n';
    }
    if(!regions.length)throw new Error('No chops were selected');
    entries.push({name:'chops.sfz',data:sfz});
    entries.push({name:'chop_map.json',data:JSON.stringify({source:options.name,bpm:options.bpm,sampleRate:audio.sampleRate,regions},null,2)+'\n'});
    entries.push({name:'pad_reference.mid',data:midiFile(options.events,options.bpm)});
    entries.push({name:'HOW_TO_IMPORT_IN_ABLETON.txt',data:[
      'CHOPROOM • ABLETON EXPORT','',
      '1. Unzip this folder. The individual WAV files are in slices/.',
      '2. Ableton: add an empty Drum Rack to a MIDI track.',
      '3. Drag WAV files in numerical order onto Drum Rack pads starting at C1 (MIDI 36).',
      '4. To edit one chop: open its Simpler device and add Ableton effects to the pad or rack.',
      '5. Optional: drag pad_reference.mid into a MIDI clip to test all 16 pads.',
      '6. chops.sfz maps the same 16 files for SFZ-compatible software samplers.','',
      'Note: The MIDI is a sequential note reference, not a recording of your performance.',
      'Audio is generated locally. The original file was not modified.',
      'Pitch/reverse and short edge fades are rendered into the individual WAVs.',''
    ].join('\n')});
    return {blob:zipStore(entries),count:regions.length,regions,entries};
  }
  function makeCollectionKit(audio,markers,banks,removed,settings,options={}) {
    if(!audio||!Array.isArray(markers)||markers.length<2)throw new Error('Load audio to export');
    const variants=Array.isArray(options.variants)?options.variants:[];
    const variantMap=new Map(variants.filter(v=>v&&typeof v.id==='string'&&
      Number.isFinite(v.start)&&Number.isFinite(v.end)&&v.end-v.start>=.03).map(v=>[v.id,v]));
    const enabled=new Set([
      ...Array.from({length:markers.length-1},(_,i)=>i),
      ...variantMap.keys()
    ].filter(i=>!removed.includes(i)));
    if(!enabled.size)throw new Error('Restore at least one chop before exporting');
    const stem=cleanName(options.name||'sample');
    const entries=[],regions=[],allBytes=new Map(),bankMaps=[];
    for(const index of enabled){
      const copy=typeof index==='string'?variantMap.get(index):null;
      const start=copy?copy.start:markers[index],end=copy?copy.end:markers[index+1],opts=settings[index]||{};
      if(end-start<.005)continue;
      const file=typeof index==='number'
        ?'chops/'+String(index+1).padStart(3,'0')+'_'+stem+'.wav'
        :'chops/COPY_'+cleanName(index)+'_'+stem+'.wav';
      const data=wav16(audio,start,end,opts);
      allBytes.set(index,data);
      entries.push({name:file,data});
      regions.push({chop:typeof index==='number'?index+1:null,variantId:copy?.id||null,
        label:copy?.label||(typeof index==='number'?'Chop '+(index+1):'Copy '+index),
        startSeconds:start,endSeconds:end,
        pitchSemitones:opts.pitch||0,reversed:!!opts.reverse,fadeMs:opts.fadeMs??8,file});
    }
    const bankList=Array.isArray(banks)?banks:[];
    for(let b=0;b<bankList.length;b++){
      const name='Bank_'+(function nameForBank(n){let out='';n++;while(n){n--;out=String.fromCharCode(65+n%26)+out;n=Math.floor(n/26);}return out;})(b);
      const pads=[];
      const sfz=['// CHOPROOM '+name+' · MIDI notes 36 to 51 (C1 to D#2)'];
      for(let pad=0;pad<16;pad++){
        const slice=bankList[b][pad];
        if(!enabled.has(slice)||!allBytes.has(slice))continue;
        const copy=typeof slice==='string'?variantMap.get(slice):null;
        const file='pad_'+String(pad+1).padStart(2,'0')+
          (copy?'_copy_'+cleanName(slice):'_chop_'+String(slice+1).padStart(3,'0'))+'.wav';
        const path='banks/'+name+'/'+file;
        entries.push({name:path,data:allBytes.get(slice)});
        pads.push({pad:pad+1,note:midiNoteName(36+pad),midi:36+pad,
          sourceChop:typeof slice==='number'?slice+1:null,
          variantId:copy?.id||null,label:copy?.label||(typeof slice==='number'?'Chop '+(slice+1):'Copy '+slice),
          file:path});
        sfz.push('<region> sample='+file+' key='+(36+pad)+' group=1 off_by=1');
      }
      bankMaps.push({bank:name,pads});
      entries.push({name:'banks/'+name+'/bank.sfz',data:sfz.join('\n')+'\n'});
      const bpm=options.bpm||90;
      const noteEvents=pads.map((p,i)=>({pad:p.pad-1,ms:i*(60000/bpm),durationMs:240,velocity:100}));
      if(noteEvents.length)entries.push({name:'banks/'+name+'/pad_reference.mid',data:midiFile(noteEvents,bpm)});
    }
    entries.push({name:'collection_map.json',data:JSON.stringify({
      source:options.name,bpm:options.bpm,sampleRate:audio.sampleRate,
      chops:regions,banks:bankMaps,
      removedChops:removed.filter(i=>typeof i==='number').map(i=>i+1),
      removedCopies:removed.filter(i=>typeof i==='string')
    },null,2)+'\n'});
    entries.push({name:'HOW_TO_IMPORT_IN_ABLETON.txt',data:[
      'CHOPROOM · COMPLETE COLLECTION','',
      'chops/ contains every active original chop and independent copy, even if unassigned.',
      'banks/Bank_A/, Bank_B/ ... contain 16-pad mappings, WAV files, SFZ presets, and MIDI references.',
      'To use a bank in Ableton: create a MIDI track with Drum Rack and drag its',
      'numbered pad WAVs to the corresponding pads starting at MIDI 36 (C1).',
      'Open another Drum Rack track for another bank if desired.',
      'The JSON file records every chop/copy range and its bank assignments.',
      'Removed chops are excluded from audio export; restore them in CHOPROOM if needed.',
      'Audio editing and export are entirely local; the original was never changed.'
    ].join('\n')});
    return {blob:zipStore(entries),count:regions.length,banks:bankMaps,regions,entries};
  }

  const API={wav16,zipStore,midiFile,midiNoteName,makeKit,makeCollectionKit,cleanName,crc32};
  if(typeof module!=='undefined'&&module.exports) module.exports=API;
  root.ChopExport=API;
})(typeof window!=='undefined'?window:globalThis);
