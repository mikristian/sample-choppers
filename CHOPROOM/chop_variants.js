/* Independent non-destructive chop copies and split variants.
 * A numeric chop ID references adjacent original cut markers.
 * A string ID vN references a time range stored separately from those
 * markers, so trimming/splitting a copy never changes its original.
 */
(function(root){
  'use strict';
  const isVariantId=x=>typeof x==='string'&&/^v[1-9][0-9]*$/.test(x);
  function normalize(data,markers,duration){
    const seen=new Set(),out=[];
    if(!Array.isArray(data))return out;
    for(const item of data){
      if(!item||!isVariantId(item.id)||seen.has(item.id)
        ||!Number.isFinite(item.start)||!Number.isFinite(item.end)
        ||item.start<0||item.end>duration||item.end-item.start<.03)continue;
      seen.add(item.id);
      out.push({id:item.id,origin:item.origin??null,start:item.start,end:item.end,
        label:typeof item.label==='string'?item.label.slice(0,100):'Copy '+item.id});
    }
    return out;
  }
  function nextId(variants){
    const max=variants.reduce((value,item)=>Math.max(value,Number((item.id||'').slice(1))||0),0);
    return 'v'+(max+1);
  }
  function getRange(ref,markers,variants){
    if(Number.isInteger(ref)&&ref>=0&&ref<markers.length-1){
      return {ref,start:markers[ref],end:markers[ref+1],origin:ref,label:'Chop '+(ref+1),variant:false};
    }
    if(isVariantId(ref)){
      const item=variants.find(v=>v.id===ref);
      if(item)return {...item,ref,variant:true};
    }
    return null;
  }
  function copyRange(ref,markers,variants){
    const range=getRange(ref,markers,variants);
    if(!range)return null;
    const id=nextId(variants);
    const root=range.origin==null?ref:range.origin;
    const prior=variants.filter(v=>v.origin===root).length;
    const originalLabel=typeof root==='number'?'Chop '+(root+1):range.label;
    const copy={id,origin:root,start:range.start,end:range.end,label:originalLabel+' · Copy '+(prior+1)};
    variants.push(copy);
    return copy;
  }
  function splitRange(ref,markers,variants,at=null){
    const range=getRange(ref,markers,variants);
    if(!range||range.end-range.start<.08)return null;
    const split=at!=null&&at>range.start+.03&&at<range.end-.03
      ?at:(range.start+range.end)/2;
    if(split-range.start<.03||range.end-split<.03)return null;
    const root=range.origin==null?ref:range.origin;
    const first={id:nextId(variants),origin:root,start:range.start,end:split,
      label:range.label+' · First half'};
    variants.push(first);
    const second={id:nextId(variants),origin:root,start:split,end:range.end,
      label:range.label+' · Second half'};
    variants.push(second);
    return [first,second];
  }
  function trimCopy(ref,markers,variants,side,time,duration,minimumGap=.03){
    const item=variants.find(v=>v.id===ref);
    if(!item||!Number.isFinite(time))return null;
    if(side==='start')item.start=Math.min(item.end-minimumGap,Math.max(0,time));
    else if(side==='end')item.end=Math.max(item.start+minimumGap,Math.min(duration,time));
    else return null;
    return item;
  }
  const API={isVariantId,normalize,nextId,getRange,copyRange,splitRange,trimCopy};
  if(typeof module!=='undefined'&&module.exports)module.exports=API;
  root.ChopVariants=API;
})(typeof window!=='undefined'?window:globalThis);
