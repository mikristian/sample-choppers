/* CHOPROOM hardware and sampler-lab preview.
 *
 * Connects only to real Web MIDI inputs selected by the user. The 49772
 * localhost bridge reports app state and can launch fixed installed apps,
 * but does not claim controller/MIDI/audio routing that it cannot verify.
 */
(function(root){
  'use strict';
  const BRIDGE='http://127.0.0.1:49772';
  const STORE='choproom-device-profile-v1';
  const PROFILES=[
    {id:'maschine',name:'MASCHINE+',sub:'Native Instruments · 16 pads',software:'maschine-lab',
     softwareName:'MASCHINE LAB',match:/maschine/i,className:'maschine',hint:'Switch your MASCHINE+ to MIDI mode. Choose its detected MIDI input, then use MIDI Learn if note assignments differ.'},
    {id:'akai',name:'Akai MPC Sample',sub:'Akai · 16-pad sampling',software:'vintage-sampler',
     softwareName:'Vintage Sampler Lab',match:/akai|\bmpc\b/i,className:'akai',hint:'Use the MPC Sample as a MIDI input when its USB/MIDI mode is available. The local Vintage Sampler Lab is an optional separate app.'},
    {id:'flx4',name:'DDJ-FLX4',sub:'Pioneer DJ · two 8-pad decks',software:'flx4-flat',
     softwareName:'FLX4 Flat 2D',match:/flx4|pioneer/i,className:'flx4',hint:'The DDJ-FLX4 can expose MIDI pad notes. Existing DJ functions stay in Rekordbox; learn the performance-pad notes intentionally.'},
    {id:'keyboard',name:'Keyboard',sub:'16 playable shortcuts',software:null,
     softwareName:'',match:null,className:'keyboard',hint:'No physical MIDI device is required. Trigger pads using 1–4, Q–R, A–F and Z–V.'}
  ];
  const KEYS=['1','2','3','4','Q','W','E','R','A','S','D','F','Z','X','C','V'];
  let ui=null;
  let inputList=[];
  let access=null;
  let selectedInput='';
  let config={};
  let activePad=0;
  let chosen='maschine';
  try{const v=localStorage.getItem(STORE);if(PROFILES.some(p=>p.id===v))chosen=v;}catch{}
  const profile=()=>PROFILES.find(p=>p.id===chosen)||PROFILES[0];

  function el(tag,cls,text){
    const node=document.createElement(tag);
    if(cls)node.className=cls;
    if(text!=null)node.textContent=text;
    return node;
  }
  function button(cls,label,listener){
    const b=el('button',cls,label);
    b.type='button';
    b.addEventListener('click',listener);
    return b;
  }
  function notify(msg,error=false){
    if(!ui)return;
    ui.detail.textContent=msg;
    ui.detail.dataset.status=error?'warning':'info';
  }
  function connectedInputs(){
    return inputList.filter(i=>i.state==='connected');
  }
  function matchingInput(){
    const actual=connectedInputs();
    if(chosen==='keyboard')return null;
    if(selectedInput)return actual.find(i=>i.id===selectedInput)||null;
    const test=profile().match;
    return actual.find(i=>test&&test.test((i.manufacturer||'')+' '+(i.name||'')))||null;
  }
  function mount(options={}){
    config=options;
    const rootEl=document.getElementById('controllerWorkspace');
    if(!rootEl)return false;
    rootEl.replaceChildren();
    rootEl.classList.add('device-workspace');
    const head=el('div','device-head');
    const headLeft=el('div','device-head-label');
    headLeft.append(el('div','device-kicker','HARDWARE · MIDI · DJ ROUTING'),
      el('h3','device-heading','Devices & Pad Preview'),
      el('p','device-intro','Choose your controller layout. Connect real MIDI hardware and assign any CHOPROOM chop to its pads.'));
    const right=el('div','device-head-actions');
    const refresh=button('device-soft-btn','Check devices',async()=>await checkStatus());
    const connect=button('device-bright-btn','Connect MIDI',async()=>{
      if(typeof config.onConnectMIDI!=='function')return notify('MIDI connection is not configured.',true);
      notify('Requesting Chrome MIDI access…');
      try{await config.onConnectMIDI();}catch(err){notify('MIDI request failed: '+err.message,true);}
    });
    right.append(refresh,connect);head.append(headLeft,right);
    const body=el('div','device-body');
    const chooser=el('div','device-profile-chooser');
    const tabs=el('div','device-profile-tabs');
    for(const p of PROFILES){
      const tab=button('device-tab',p.name,()=>selectProfile(p.id));
      tab.dataset.device=p.id;
      tab.title=p.sub;
      tabs.append(tab);
    }
    const helper=el('p','device-profile-helper','');
    const matchLine=el('div','device-live-state');
    const statusLight=el('span','device-status-light');
    const inputText=el('span','device-input-text','No MIDI input selected');
    matchLine.append(statusLight,inputText);
    const midiLine=el('div','device-input-row');
    const label=el('label','device-input-label','MIDI INPUT');
    const inputSelect=el('select','device-input-select');
    inputSelect.setAttribute('aria-label','Choose a detected MIDI device');
    inputSelect.addEventListener('change',()=>{
      selectedInput=inputSelect.value;
      renderConnection();
    });
    label.append(inputSelect);
    midiLine.append(label);
    const appLine=el('div','device-app-row');
    const launch=button('device-soft-btn','Open sampler app',()=>launchApp());
    const note=el('div','device-app-status','Software application state is separate from live MIDI connection.');
    appLine.append(launch,note);
    chooser.append(tabs,helper,matchLine,midiLine,appLine);
    const preview=el('div','device-preview-column');
    const padBox=el('div','device-preview-box');
    const padHeader=el('div','device-pad-heading');
    const labelSpan=el('div','device-preview-name','CONTROLLER VIEW');
    const current=el('div','device-preview-current','BANK A');
    padHeader.append(labelSpan,current);
    const housing=el('div','device-housing');
    const screens=el('div','device-screens');
    screens.append(el('div','device-small-screen','CHOPROOM'),el('div','device-small-screen','PLAY / LEARN'));
    const matrix=el('div','device-pad-grid');
    const pads=[];
    for(let i=0;i<16;i++){
      const pad=button('device-preview-pad','',()=>{
        activePad=i;
        if(typeof config.onSelectPad==='function')config.onSelectPad(i);
        if(typeof config.onPreviewPad==='function')config.onPreviewPad(i);
        renderPads();
      });
      pad.dataset.index=String(i);
      const small=el('span','device-preview-pad-note',String(i+1).padStart(2,'0'));
      const name=el('span','device-preview-pad-name','—');
      const key=el('span','device-preview-pad-key',KEYS[i]);
      pad.append(small,name,key);
      pads.push(pad);
      matrix.append(pad);
    }
    const padFooter=el('div','device-pad-footer');
    const selected=el('div','device-pad-selected','PAD 01 · No chop');
    const assign=button('device-bright-btn','Load selected chop → pad',()=>{
      if(typeof config.onAssignChop!=='function')return notify('Cannot assign a chop in this view.',true);
      const result=config.onAssignChop(activePad);
      if(result===false){notify('Select an active chop in the source editor first.',true);return;}
      renderPads();
      notify('Loaded the selected chop on Pad '+String(activePad+1).padStart(2,'0')+'.');
    });
    padFooter.append(selected,assign);
    housing.append(screens,matrix);
    padBox.append(padHeader,housing,padFooter);
    const tip=el('p','device-tip','MIDI is recognized only after Chrome grants permission and the selected hardware is detected.');
    preview.append(padBox,tip);
    body.append(chooser,preview);
    const footer=el('div','device-rekordbox');
    const rbText=el('div','device-rekordbox-text');
    const rbTitle=el('strong',null,'Rekordbox / DDJ-FLX4');
    const rbState=el('span','device-rekordbox-status','Checking local Rekordbox and Music Hub…');
    rbText.append(rbTitle,rbState);
    const rbActions=el('div','device-rekordbox-actions');
    const openRb=button('device-soft-btn','Open Rekordbox',()=>openRekordbox());
    const openHub=button('device-soft-btn','Open Music Hub',()=>{
      window.open('http://127.0.0.1:18769/','_blank','noopener,noreferrer');
    });
    rbActions.append(openRb,openHub);footer.append(rbText,rbActions);
    const detail=el('p','device-detail','MIDI input and Rekordbox app status are verified separately.');
    rootEl.append(head,body,footer,detail);
    ui={root:rootEl,chooser,tabs,helper,matchLine,statusLight,inputText,inputSelect,
      appLine,launch,note,padBox,matrix,pads,current,selected,detail,rbState,connect};
    selectProfile(chosen);
    checkStatus();
    return true;
  }
  function selectProfile(next){
    if(!PROFILES.some(p=>p.id===next))return;
    chosen=next;
    selectedInput='';
    activePad=Number.isInteger(config.getSelectedPad?.())?config.getSelectedPad():0;
    try{localStorage.setItem(STORE,chosen);}catch{}
    if(!ui)return;
    ui.root.dataset.profile=chosen;
    ui.helper.textContent=profile().hint;
    ui.launch.textContent=profile().software?'Open '+profile().softwareName:'Keyboard needs no app';
    ui.launch.disabled=!profile().software;
    ui.tabs.querySelectorAll('button').forEach(b=>{
      const selected=b.dataset.device===chosen;
      b.classList.toggle('active',selected);
      b.setAttribute('aria-pressed',String(selected));
    });
    renderConnection();
    renderPads();
    notify(chosen==='keyboard'?'Keyboard pad mode opened.':'Showing '+profile().name+' pad layout. Choose a detected MIDI input to connect it.');
  }
  function setMidiAccess(midi){
    access=midi;
    inputList=midi ? Array.from(midi.inputs.values()).map(input=>({
      id:input.id,name:input.name||'Unnamed device',
      manufacturer:input.manufacturer||'',state:input.state,
    })) : [];
    if(access){
      access.onstatechange=()=>{
        inputList=Array.from(access.inputs.values()).map(input=>({
          id:input.id,name:input.name||'Unnamed device',
          manufacturer:input.manufacturer||'',state:input.state,
        }));
        renderConnection();
      };
    }
    renderConnection();
  }
  function shouldHandleInput(input){
    if(chosen==='keyboard')return false;
    if(!input)return false;
    const selected=matchingInput();
    return Boolean(selected && input.id===selected.id);
  }
  function renderConnection(){
    if(!ui)return;
    const previous=ui.inputSelect.value;
    ui.inputSelect.replaceChildren();
    const automatic=el('option',null,'Auto-detect '+profile().name);
    automatic.value='';
    ui.inputSelect.append(automatic);
    for(const dev of connectedInputs()){
      const option=el('option',null,dev.name+(dev.manufacturer?' · '+dev.manufacturer:''));
      option.value=dev.id;
      ui.inputSelect.append(option);
    }
    if(selectedInput && !connectedInputs().some(i=>i.id===selectedInput))selectedInput='';
    ui.inputSelect.value=selectedInput;
    const connected=matchingInput();
    const keyboard=chosen==='keyboard';
    ui.inputText.textContent=keyboard?'Computer keyboard · 16 pads ready'
      :connected?'Detected: '+connected.name
      :!access?'MIDI permission not requested yet'
      :connectedInputs().length?'No '+profile().name+' MIDI device selected or matched'
      :'No MIDI devices detected';
    ui.statusLight.classList.toggle('connected',!!connected||keyboard);
    ui.statusLight.classList.toggle('unverified',!connected&&!keyboard);
    ui.connect.textContent=access?'Refresh MIDI':'Connect MIDI';
  }
  function renderPads(){
    if(!ui)return;
    const bankName=typeof config.getBankName==='function'?config.getBankName():'Bank A';
    ui.current.textContent=bankName.toUpperCase();
    const source=typeof config.getPadSource==='function'?config.getPadSource:null;
    for(let i=0;i<16;i++){
      const ref=source?source(i):null;
      const p=ui.pads[i];
      p.classList.toggle('selected',i===activePad);
      p.classList.toggle('assigned',ref!=null);
      p.setAttribute('aria-pressed',String(i===activePad));
      p.querySelector('.device-preview-pad-name').textContent=ref==null?'EMPTY':String(ref);
    }
    const ref=source?source(activePad):null;
    ui.selected.textContent='PAD '+String(activePad+1).padStart(2,'0')+
      (ref==null?' · Empty':' · '+ref);
  }
  function selectPad(i){
    if(!Number.isInteger(i)||i<0||i>=16)return;
    activePad=i;renderPads();
  }
  function markTriggered(i){
    if(!ui||i<0||i>15)return;
    const pad=ui.pads[i];
    pad.classList.add('triggered');
    // Use a promise rather than a timer; CSS animation removes itself on end.
    pad.addEventListener('animationend',()=>pad.classList.remove('triggered'),{once:true});
    selectPad(i);
  }
  async function fetchJSON(route,opts={}){
    const response=await fetch(BRIDGE+route,{
      cache:'no-store',mode:'cors',credentials:'omit',...opts,
    });
    const parsed=await response.json();
    if(!response.ok)throw new Error(parsed.error||'Local device bridge unavailable');
    return parsed;
  }
  async function checkStatus(){
    if(!ui)return;
    try{
      const r=await fetchJSON('/api/status');
      const rb=r.rekordbox||{};
      const parts=[rb.running?'App running':'App not running'];
      parts.push(rb.bridgeConnected?'DJ bridge connected':'DJ bridge not verified');
      if(rb.verifiedDecks)parts.push('deck status verified');
      else parts.push('deck status unavailable');
      ui.rbState.textContent=parts.join(' · ');
      const catalog=(r.musicHub?.items)||[];
      const installed=catalog.find(d=>d.id===profile().software);
      ui.note.textContent=installed
        ?(installed.running?'Sampler app running':'Sampler app not running')+
          (installed.bridgeAvailable?' · bridge available':' · MIDI routing not verified')
        :'Hardware profile shown; choose MIDI input to use its pads.';
      return r;
    }catch(err){
      ui.rbState.textContent='Device bridge offline · Rekordbox control needs CHOPROOM launcher';
      ui.note.textContent='Preview works. Local app launcher/status is offline.';
      return null;
    }
  }
  async function openRekordbox(){
    if(!ui)return;
    notify('Opening the installed Rekordbox application…');
    try{
      const r=await fetchJSON('/api/rekordbox/open',{
        method:'POST',headers:{'Content-Type':'application/json'},body:'{}',
      });
      notify(r.openRequested?'Rekordbox open requested. MIDI/audio routing must be configured separately.':'Rekordbox did not confirm an open request.');
      await checkStatus();
    }catch(err){notify(err.message,true);}
  }
  async function launchApp(){
    if(!ui||!profile().software)return;
    try{
      const r=await fetchJSON('/api/instruments/launch',{
        method:'POST',headers:{'Content-Type':'application/json'},
        body:JSON.stringify({id:profile().software}),
      });
      notify(r.launched?profile().softwareName+' launch requested. Physical MIDI connection remains separate.':'The app did not confirm launch.');
      await checkStatus();
    }catch(err){notify('Could not launch sampler app: '+err.message,true);}
  }
  root.ChopDevices={
    mount,setMidiAccess,shouldHandleInput,renderPads,selectPad,markTriggered,
    checkStatus,selectProfile,get selectedProfile(){return chosen;},
    get selectedInput(){return matchingInput()?.name||null;},
    get midiAccess(){return access;},
  };
  if(typeof module!=='undefined'&&module.exports)module.exports={
    PROFILES,KEYS,
    profileMatches(input,name){
      const p=PROFILES.find(x=>x.id===name);
      return !!(p?.match?.test((input.manufacturer||'')+' '+(input.name||'')));
    },
  };
})(typeof window!=='undefined'?window:globalThis);
