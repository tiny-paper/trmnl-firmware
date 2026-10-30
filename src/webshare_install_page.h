#pragma once

// The page of "Install via Wi-Fi" (webshare_install.cpp). File names are shown with textContent only.
static const char WEBSHARE_INSTALL_PAGE[] = R"HTML(<!doctype html>
<html lang=en><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Install apps</title>
<style>
body{font:16px/1.4 system-ui,sans-serif;max-width:42rem;margin:0 auto;padding:1rem;color:#111;background:#fff}
h1{font-size:1.3rem}button,input{font:inherit;padding:.4rem .7rem;margin:.15rem 0}
.row{display:flex;gap:.5rem;align-items:center;border-bottom:1px solid #ddd;padding:.3rem 0;flex-wrap:wrap}
.row .n{flex:1;min-width:8rem;word-break:break-all}.s{color:#666;font-size:.85rem}
#msg{white-space:pre-line;margin:.6rem 0;padding:.5rem;background:#eef;border-radius:.3rem}#msg.bad{background:#fdd}
@media(prefers-color-scheme:dark){body{background:#111;color:#eee}.row{border-color:#444}#msg{background:#224}#msg.bad{background:#522}}
</style></head><body>
<h1>Install apps on the X</h1>
<div id=msg hidden></div>
<div id=login><p>Type the 6-digit code shown on the X.</p>
<input id=pin inputmode=numeric pattern="[0-9]*" maxlength=6 autocomplete=off size=8> <button id=go>Open</button></div>
<div id=main hidden>
<div id=apps></div>
<p>Add an app (.elf):</p><input id=up type=file accept=".elf" multiple>
<p class=s>Every file is checked before it is kept. An app with the same name is kept as .elf.bak until the new one has
started once; if it fails to start the old one comes back. From a computer:<br>
<code>curl -F file=@app.elf "http://&lt;this address&gt;/apps?code=&lt;code&gt;"</code></p>
</div>
<script>
const $=id=>document.getElementById(id),H={'X-Writer':'1'};
function say(t,bad){const m=$('msg');m.textContent=t;m.className=bad?'bad':'';m.hidden=!t}
function show(ok){$('login').hidden=ok;$('main').hidden=!ok}
async function api(path,opt){
  try{const r=await fetch(path,Object.assign({headers:H,credentials:'same-origin'},opt||{}));
    if(r.status==401)show(false);
    let j={};try{j=await r.json()}catch(e){}
    return{ok:r.ok,status:r.status,j}}
  catch(e){return{ok:false,status:0,j:{error:'no connection (the X switched the page off?)'}}}}
function kb(n){return n<1024?n+' B':(n/1024).toFixed(1)+' KB'}
async function load(){
  const r=await api('/api/apps');if(!r.ok){if(r.status!=401)say(r.j.error||'Failed',true);return}
  show(true);const box=$('apps');box.textContent='';
  if(r.j.note)say(r.j.note,true);
  r.j.apps.forEach(a=>{
    const d=document.createElement('div');d.className='row';
    const n=document.createElement('span');n.className='n';n.textContent=a.n+(a.bak?' (older version kept)':'');d.appendChild(n);
    const s=document.createElement('span');s.className='s';
    s.textContent=kb(a.s)+(a.d?', '+new Date(a.d*1000).toLocaleString():'');d.appendChild(s);
    const x=document.createElement('button');x.textContent='Delete';x.onclick=async()=>{
      if(!confirm('Delete '+a.n+' from the X?'))return;
      const res=await api('/api/appdelete?name='+encodeURIComponent(a.n),{method:'POST'});
      say(res.ok?'Deleted.':(res.j.error||'Failed'),!res.ok);load()};d.appendChild(x);box.appendChild(d)});
  if(!r.j.apps.length)box.textContent='No apps yet.'}
$('go').onclick=async()=>{
  const r=await api('/api/login',{method:'POST',body:'pin='+encodeURIComponent($('pin').value),
    headers:Object.assign({'Content-Type':'application/x-www-form-urlencoded'},H)});
  if(r.ok){say('');load()}else say(r.j.error||'Failed',true)};
$('pin').onkeydown=e=>{if(e.key=='Enter')$('go').click()};
$('up').onchange=async e=>{
  const out=[];
  for(const f of e.target.files){
    const r=await api('/api/app?name='+encodeURIComponent(f.name),{method:'POST',body:f});
    out.push(f.name+': '+(r.ok?'installed':'refused: '+(r.j.error||'failed')))}
  say(out.join('\n'));e.target.value='';load()};
load();
</script></body></html>)HTML";
