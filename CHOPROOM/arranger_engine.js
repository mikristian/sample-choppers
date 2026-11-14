/* CHOPROOM arrangement model. All operations return new snapshots; source audio and chops are untouched. */
(function(root){
  'use strict';
  const MIN=.03, MAX_TRACKS=32, MAX_CLIPS=200;
  const finite=(n,f=0)=>Number.isFinite(Number(n))?Number(n):f;
  const clamp=(n,a,b)=>Math.max(a,Math.min(b,n));
  const track=(id,name,auto=false)=>({id,name:name||'Audio '+id,mute:false,solo:false,auto});
  const fresh=()=>({version:2,tracks:[track('t1','Auto chops',true),track('t2','Manual hooks',false)],clips:[],sourceSeen:[],zoom:80,view:'fit',snap:'off',selected:null,playhead:0,nextId:1});
  const copy=s=>({ ...s,tracks:s.tracks.map(t=>({...t})),clips:s.clips.map(c=>({...c})),sourceSeen:[...(s.sourceSeen||[])]});
  const sourceToken=(trackId,ref,a,b)=>[trackId,String(ref),Math.round(a*1000),Math.round(b*1000)].join('|');
  const validRef=r=>Number.isInteger(r)&&r>=0||(typeof r==='string'&&/^v[1-9]\d*$/.test(r));
  function normalize(raw){
    const result=fresh();
    if(!raw||typeof raw!=='object')return result;
    result.tracks=Array.isArray(raw.tracks)?raw.tracks.slice(0,MAX_TRACKS).filter(x=>x&&typeof x.id==='string'&&x.id.length<80).map(t=>({id:t.id,name:String(t.name||'Audio').slice(0,64),mute:!!t.mute,solo:!!t.solo,auto:typeof t.auto==='boolean'?t.auto:t.id==='t1'})):result.tracks;
    result.tracks=result.tracks.filter((t,i,a)=>a.findIndex(u=>u.id===t.id)===i);
    if(!result.tracks.length)result.tracks=[track('t1','Arrangement 1')];
    const used=new Set();
    result.clips=(Array.isArray(raw.clips)?raw.clips:[]).slice(0,MAX_CLIPS).filter(c=>{
      if(!c||typeof c.id!=='string'||used.has(c.id)||!validRef(c.ref)||!result.tracks.some(t=>t.id===c.trackId))return false;
      if(![c.sourceStart,c.sourceEnd,c.start].every(Number.isFinite)||c.sourceStart<0||c.sourceEnd-c.sourceStart<MIN||c.start<0)return false;
      used.add(c.id);return true;
    }).map(c=>({id:c.id,ref:c.ref,trackId:c.trackId,sourceStart:c.sourceStart,sourceEnd:c.sourceEnd,start:c.start,label:String(c.label||'Chop').slice(0,100)}));
    const existing=result.clips.filter(c=>result.tracks.some(t=>t.id===c.trackId&&t.auto))
      .map(c=>sourceToken(c.trackId,c.ref,c.sourceStart,c.sourceEnd));
    result.sourceSeen=[...new Set([...(Array.isArray(raw.sourceSeen)?raw.sourceSeen.filter(v=>typeof v==='string'&&v.length<180):[]),...existing])].slice(-1600);
    result.nextId=Math.max(1,Math.floor(finite(raw.nextId,1)),...result.clips.map(c=>Number(/^c(\d+)$/.exec(c.id)?.[1]||0)+1));
    result.zoom=clamp(finite(raw.zoom,80),20,320);
    // Fit is the default even when opening older sessions that predated
    // the zoom selector. Manual zoom remains available as 'detail'.
    result.view=raw.view==='detail'?'detail':'fit';
    result.snap=['off','beat','bar'].includes(raw.snap)?raw.snap:'off';
    result.selected=result.clips.some(c=>c.id===raw.selected)?raw.selected:null;
    result.playhead=Math.max(0,finite(raw.playhead));
    return result;
  }
  const snapSeconds=(value,snap,tempo=90)=>{
    const beat=60/clamp(finite(tempo,90),30,300);
    return snap==='beat'?Math.round(value/beat)*beat:snap==='bar'?Math.round(value/(beat*4))*beat*4:value;
  };
  function addTrack(state,name){const s=copy(normalize(state));if(s.tracks.length>=MAX_TRACKS)return s;
    let n=1;while(s.tracks.some(t=>t.id==='t'+n))n++;
    s.tracks.push(track('t'+n,name||'Arrangement '+(s.tracks.length+1)));return s;}
  function setTrack(state,id,fields){const s=copy(normalize(state));const t=s.tracks.find(x=>x.id===id);
    if(t){if('mute'in fields)t.mute=!!fields.mute;if('solo'in fields)t.solo=!!fields.solo;if('auto'in fields)t.auto=!!fields.auto;if('name'in fields)t.name=String(fields.name||'Audio').slice(0,64);}return s;}
  function populateFromChops(state,sourceRows,gap=.55){
    const s=copy(normalize(state));
    if(!Array.isArray(sourceRows)||!s.tracks.some(t=>t.auto))return s;
    const seen=new Set(s.sourceSeen);
    const spacing=clamp(finite(gap,.55),.08,4);
    for(const lane of s.tracks.filter(t=>t.auto)){
      let cursor=s.clips.filter(c=>c.trackId===lane.id)
        .reduce((n,c)=>Math.max(n,c.start+c.sourceEnd-c.sourceStart),0);
      for(const source of sourceRows){
        if(s.clips.length>=MAX_CLIPS)break;
        if(!source||!validRef(source.ref)||!Number.isFinite(source.start)||!Number.isFinite(source.end)
          ||source.start<0||source.end-source.start<MIN)continue;
        const token=sourceToken(lane.id,source.ref,source.start,source.end);
        if(seen.has(token))continue;
        // A near-identical previous cut simply had a marker nudged. It
        // should not create another clip every time the marker moves.
        const older=s.clips.find(c=>c.trackId===lane.id&&c.ref===source.ref&&
          Math.abs(c.sourceStart-source.start)<.04&&Math.abs(c.sourceEnd-source.end)<.04);
        seen.add(token);
        if(older)continue;
        const prevSelected=s.selected;
        const result=addClip(s,source.ref,source,{trackId:lane.id,start:cursor>0?cursor+spacing:0});
        s.clips=result.clips;s.nextId=result.nextId;s.selected=prevSelected;
        cursor=s.clips.filter(c=>c.trackId===lane.id).reduce((n,c)=>Math.max(n,c.start+c.sourceEnd-c.sourceStart),0);
      }
    }
    s.sourceSeen=[...seen].slice(-1600);
    return s;
  }
  function resetAutoRows(state){
    const s=copy(normalize(state));
    const autoTracks=new Set(s.tracks.filter(t=>t.auto).map(t=>t.id));
    s.clips=s.clips.filter(c=>!autoTracks.has(c.trackId));
    s.sourceSeen=s.sourceSeen.filter(key=>![...autoTracks].some(id=>key.startsWith(id+'|')));
    if(!s.clips.some(c=>c.id===s.selected))s.selected=null;
    return s;
  }
  function addClip(state,ref,range,options={}){const s=copy(normalize(state));if(s.clips.length>=MAX_CLIPS||!validRef(ref)||!range)return s;
    const a=finite(range.start,NaN),b=finite(range.end,NaN);if(!Number.isFinite(a)||!Number.isFinite(b)||a<0||b-a<MIN)return s;
    const trackId=s.tracks.some(t=>t.id===options.trackId)?options.trackId:s.tracks[0].id;
    const id='c'+s.nextId++;s.clips.push({id,ref,trackId,sourceStart:a,sourceEnd:b,start:Math.max(0,finite(options.start,s.playhead)),label:String(range.label||'Chop').slice(0,100)});
    s.selected=id;return s;}
  function moveClip(state,id,at,trackId,tempo){const s=copy(normalize(state));const c=s.clips.find(x=>x.id===id);if(!c)return s;
    c.start=Math.max(0,snapSeconds(Math.max(0,finite(at,c.start)),s.snap,tempo));
    if(s.tracks.some(t=>t.id===trackId))c.trackId=trackId;return s;}
  function trimClip(state,id,side,delta,tempo,sourceDuration=Infinity){const s=copy(normalize(state));const c=s.clips.find(x=>x.id===id);if(!c)return s;
    const d=finite(delta,0),duration=c.sourceEnd-c.sourceStart;
    if(side==='left'){
      const change=clamp(snapSeconds(c.start+d,s.snap,tempo)-c.start,-Math.min(c.sourceStart,c.start),duration-MIN);
      c.start+=change;c.sourceStart+=change;
    }else if(side==='right'){
      const end=c.start+duration;
      const change=snapSeconds(end+d,s.snap,tempo)-end;
      const limit=Number.isFinite(sourceDuration)?Math.max(c.sourceStart+MIN,sourceDuration):Infinity;
      c.sourceEnd=clamp(c.sourceEnd+change,c.sourceStart+MIN,limit);
    }return s;}
  function splitClip(state,id,at){const s=copy(normalize(state)),c=s.clips.find(x=>x.id===id);
    if(!c||s.clips.length>=MAX_CLIPS||!Number.isFinite(at))return s;
    const offset=at-c.start,oldDuration=c.sourceEnd-c.sourceStart;
    if(offset<MIN||oldDuration-offset<MIN)return s;
    const originalEnd=c.sourceEnd,cut=c.sourceStart+offset;
    c.sourceEnd=cut;
    const second={...c,id:'c'+s.nextId++,start:at,sourceStart:cut,sourceEnd:originalEnd};
    s.clips.splice(s.clips.indexOf(c)+1,0,second);s.selected=second.id;return s;
  }
  function removeClip(state,id){const s=copy(normalize(state));s.clips=s.clips.filter(c=>c.id!==id);if(s.selected===id)s.selected=null;return s;}
  function duplicateClip(state,id,at,trackId,tempo){const s=copy(normalize(state)),c=s.clips.find(x=>x.id===id);if(!c||s.clips.length>=MAX_CLIPS)return s;
    const id2='c'+s.nextId++;
    const start=Math.max(0,snapSeconds(finite(at,c.start+c.sourceEnd-c.sourceStart),s.snap,tempo));
    s.clips.push({...c,id:id2,start,trackId:s.tracks.some(t=>t.id===trackId)?trackId:c.trackId});s.selected=id2;return s;}
  function selectClip(state,id){const s=copy(normalize(state));s.selected=s.clips.some(c=>c.id===id)?id:null;return s;}
  function setView(state,fields){const s=copy(normalize(state));if('zoom'in fields)s.zoom=clamp(finite(fields.zoom,s.zoom),20,320);
    if('view'in fields&&['fit','detail'].includes(fields.view))s.view=fields.view;
    if('snap'in fields&&['off','beat','bar'].includes(fields.snap))s.snap=fields.snap;
    if('playhead'in fields)s.playhead=Math.max(0,finite(fields.playhead,s.playhead));return s;}
  const audible=(state,c)=>{const s=normalize(state),t=s.tracks.find(x=>x.id===c.trackId);return !!t&&!t.mute&&(!s.tracks.some(x=>x.solo)||t.solo);};
  const duration=state=>normalize(state).clips.reduce((n,c)=>Math.max(n,c.start+c.sourceEnd-c.sourceStart),0);
  const api={MIN,MAX_TRACKS,MAX_CLIPS,create:fresh,normalize,addTrack,setTrack,addClip,moveClip,trimClip,splitClip,deleteClip:removeClip,removeClip,duplicateClip,selectClip,setView,snapSeconds,audible,duration,populateFromChops,resetAutoRows};
  if(typeof module!=='undefined'&&module.exports)module.exports=api;
  root.ChopArrangerEngine=api;
})(typeof window!=='undefined'?window:globalThis);
