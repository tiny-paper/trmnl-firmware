#pragma once

// /kbd.js (webshare_settings.cpp): the X's keyboard layout as a menu in a page's <div id=kbd>, after login. Every
// profile's page loads it and calls kbd() once logged in. Names are shown with textContent only.
static const char WEBSHARE_KBD_JS[] = R"JS((function(){
const H={'X-Writer':'1'};
window.kbd=async function(){
  const box=document.getElementById('kbd');if(!box)return;
  let r;try{r=await fetch('/api/kbd',{headers:H,credentials:'same-origin'})}catch(e){return}
  if(!r.ok)return;const j=await r.json();box.textContent='';
  const l=document.createElement('label');l.textContent='Keyboard layout of the X: ';
  const s=document.createElement('select');
  j.layouts.forEach(x=>{const o=document.createElement('option');o.value=x.c;o.textContent=x.n;o.selected=x.c==j.cur;s.appendChild(o)});
  const m=document.createElement('span');m.className='s';
  s.onchange=async()=>{let q={ok:false};try{q=await fetch('/api/kbd',{method:'POST',credentials:'same-origin',
    headers:Object.assign({'Content-Type':'application/x-www-form-urlencoded'},H),body:'c='+encodeURIComponent(s.value)})}catch(e){}
    m.textContent=q.ok?' saved':' not saved'};
  l.appendChild(s);box.appendChild(l);box.appendChild(m)};
kbd();
})();)JS";
