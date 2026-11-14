/* Quick navigation between large CHOPROOM workspaces.
 * Scroll-only: never changes source audio, project, playback or bank data.
 */
(() => {
  'use strict';
  const buttons=[...document.querySelectorAll('.rail-nav [data-nav-target]')];
  const entries=buttons.map(button=>({
    button,target:document.getElementById(button.dataset.navTarget),
  })).filter(entry=>entry.target);
  if(!entries.length)return;
  let manuallyChosen=null;
  function markActive(id){
    for(const {button,target} of entries){
      const active=target.id===id;
      button.classList.toggle('active',active);
      if(active)button.setAttribute('aria-current','location');
      else button.removeAttribute('aria-current');
    }
  }
  entries.forEach(({button,target})=>{
    button.addEventListener('click',()=>{
      manuallyChosen=target.id;
      markActive(target.id);
      target.scrollIntoView({behavior:'smooth',block:'start'});
    });
  });
  let scrollHandle=0;
  window.addEventListener('scroll',()=>{
    if(scrollHandle)return;
    scrollHandle=requestAnimationFrame(()=>{
      scrollHandle=0;
      const visible=entries.map(({target})=>{
        const box=target.getBoundingClientRect();
        return {id:target.id,top:box.top,bottom:box.bottom};
      }).filter(box=>box.bottom>100 && box.top<window.innerHeight*.8);
      // Prefer the nearest visible section to the top of the viewport,
      // except while a manually chosen destination is still scrolling in.
      const chosen=visible.find(e=>e.id===manuallyChosen);
      if(chosen && chosen.top>=-130 && chosen.top<=180){
        markActive(chosen.id);
        manuallyChosen=null;
        return;
      }
      if(manuallyChosen)return;
      const nearest=visible.sort((a,b)=>Math.abs(a.top-110)-Math.abs(b.top-110))[0];
      if(nearest)markActive(nearest.id);
    });
  },{passive:true});
})();
