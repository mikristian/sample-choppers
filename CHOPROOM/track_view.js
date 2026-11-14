/* Coordinate helpers for scroll navigation and the mini chop editor.
 * Keeping these pure makes gesture boundaries reproducible without audio.
 */
(function(root){
  'use strict';
  function clamp(n,min,max){return Math.max(min,Math.min(max,n));}
  function previewWindow(start,end,duration){
    if(!Number.isFinite(duration)||duration<=0)return {from:0,span:1};
    const width=Math.max(.03,end-start);
    const span=Math.min(duration,Math.max(1.5,width*1.5));
    const middle=(start+end)/2;
    return {from:clamp(middle-span/2,0,Math.max(0,duration-span)),span};
  }
  function timeAtX(x,width,from,span){
    if(!Number.isFinite(width)||width<=0)return from;
    return from+clamp(x/width,0,1)*span;
  }
  function xAtTime(time,width,from,span){
    if(span<=0)return 0;
    return (time-from)/span*width;
  }
  function nearestSide(start,end,from,span,width,x,radius=17){
    const left=xAtTime(start,width,from,span);
    const right=xAtTime(end,width,from,span);
    const l=Math.abs(x-left),r=Math.abs(x-right);
    if(Math.min(l,r)>radius)return null;
    return l<=r?'start':'end';
  }
  function panScroll(current,visible,total,pixels,width){
    if(!Number.isFinite(current)||!Number.isFinite(visible)||visible<=0||!Number.isFinite(pixels)||width<=0)return current;
    const maximum=Math.max(0,total-visible);
    return clamp(current+pixels/width*visible,0,maximum);
  }
  const API={previewWindow,timeAtX,xAtTime,nearestSide,panScroll};
  if(typeof module!=='undefined'&&module.exports)module.exports=API;
  root.ChopTrackView=API;
})(typeof window!=='undefined'?window:globalThis);
