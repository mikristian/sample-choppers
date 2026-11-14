/* Multi-bank pad assignment and reversible chop removal.
 * All references are slice indices. Each boundary insertion updates the
 * indices atomically, so pad sounds do not change unexpectedly.
 */
(function(root){
  'use strict';
  const SIZE=16;
  const emptyBank=()=>Array(SIZE).fill(null);
  function fresh(count=1){
    const banks=Array.from({length:Math.max(1,count)},emptyBank);
    banks[0][0]=0;
    return {banks,removed:[]};
  }
  function normalize(raw, count, variantIds=[]){
    const variants=new Set(variantIds);
    const banks=(Array.isArray(raw)&&raw.length?raw:[emptyBank()]).map(items=>{
      const out=emptyBank();
      if(Array.isArray(items))for(let i=0;i<SIZE;i++){
        const n=items[i];
        if((Number.isInteger(n)&&n>=0&&n<count)||(typeof n==='string'&&variants.has(n)))out[i]=n;
      }
      return out;
    });
    return banks;
  }
  function assignFirstAvailable(banks,slice){
    for(let i=0;i<banks.length;i++){
      const slot=banks[i].indexOf(null);
      if(slot>=0){banks[i][slot]=slice;return {bank:i,pad:slot};}
    }
    const bank=emptyBank();bank[0]=slice;
    banks.push(bank);return {bank:banks.length-1,pad:0};
  }
  function shiftForInsertion(banks,removed,markerIndex,oldMarkerCount){
    const freshSlice=markerIndex===oldMarkerCount?oldMarkerCount-1:markerIndex;
    for(const bank of banks)for(let p=0;p<SIZE;p++){
      if(typeof bank[p]==='number'&&bank[p]>=markerIndex)bank[p]+=1;
    }
    const shifted=removed.map(i=>typeof i==='number'&&i>=markerIndex?i+1:i);
    const assigned=assignFirstAvailable(banks,freshSlice);
    return {removed:shifted,freshSlice,assigned};
  }
  function removeChop(banks,removed,slice){
    if(removed.includes(slice))return false;
    removed.push(slice);
    for(const bank of banks)for(let p=0;p<SIZE;p++)if(bank[p]===slice)bank[p]=null;
    return true;
  }
  function restoreChop(banks,removed,slice){
    const i=removed.indexOf(slice);
    if(i<0)return false;
    removed.splice(i,1);
    assignFirstAvailable(banks,slice);
    return true;
  }
  function shiftForMerge(banks,removed,markerIndex){
    const removedSlice=markerIndex;
    for(const bank of banks)for(let p=0;p<SIZE;p++){
      const i=bank[p];
      if(i==null)continue;
      if(i===removedSlice)bank[p]=null;
      else if(typeof i==='number'&&i>removedSlice)bank[p]=i-1;
    }
    return removed.filter(i=>i!==markerIndex&&i!==markerIndex-1)
      .map(i=>typeof i==='number'&&i>markerIndex?i-1:i);
  }
  function movePad(banks,fromBank,fromPad,toBank,toPad){
    if(!Array.isArray(banks)||[fromBank,fromPad,toBank,toPad].some(n=>!Number.isInteger(n))
      ||fromBank<0||toBank<0||fromBank>=banks.length||toBank>=banks.length
      ||fromPad<0||toPad<0||fromPad>=SIZE||toPad>=SIZE)return {error:'invalid'};
    if(fromBank===toBank&&fromPad===toPad)return {error:'same'};
    const source=banks[fromBank][fromPad],target=banks[toBank][toPad];
    if(source==null)return {error:'empty'};
    // Always swap occupied pads; when dropping onto empty, move instead.
    banks[toBank][toPad]=source;
    banks[fromBank][fromPad]=target??null;
    return {source,target,moved:true,swapped:target!=null};
  }
  const API={SIZE,emptyBank,fresh,normalize,assignFirstAvailable,shiftForInsertion,removeChop,restoreChop,shiftForMerge,movePad};
  if(typeof module!=='undefined'&&module.exports)module.exports=API;
  root.ChopCollection=API;
})(typeof window!=='undefined'?window:globalThis);
