#pragma once

// The page of the files profile (webshare_files.cpp): the same look as the writer's and the apps page, the touch bar's
// buttons included. What it accepts comes from /api/files (the app's extensions and size limit). File names are shown
// with textContent only.
static const char WEBSHARE_FILES_PAGE[] = R"HTML(<!doctype html>
<html lang=en><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Add files</title>
<style>
body{font:16px/1.4 system-ui,sans-serif;max-width:42rem;margin:0 auto;padding:1rem;color:#111;background:#fff}
#pin{font-size:2.4rem;letter-spacing:.3em;width:7em;padding:.2em}#go{font-size:1.6rem;padding:.3em .8em}
h1{font-size:1.3rem}button,input{font:inherit;padding:.4rem .7rem;margin:.15rem 0}
.row{display:flex;gap:.5rem;align-items:center;border-bottom:1px solid #ddd;padding:.3rem 0;flex-wrap:wrap}
.row .n{flex:1;min-width:8rem;word-break:break-all}.s{color:#666;font-size:.85rem}
#msg{white-space:pre-line;margin:.6rem 0;padding:.5rem;background:#eef;border-radius:.3rem}#msg.bad{background:#fdd}
@media(prefers-color-scheme:dark){body{background:#111;color:#eee}.row{border-color:#444}#msg{background:#224}#msg.bad{background:#522}}
</style></head><body>
<h1>Add files to the X</h1>
<div id=msg hidden></div>
<div id=login><p>Type the 6-digit code shown on the X.</p>
<input id=pin inputmode=numeric pattern="[0-9]*" maxlength=6 autocomplete=off size=8> <button id=go>Open</button></div>
<div id=main hidden>
<div id=files></div><p class=s id=free></p>
<p>Touch bar:</p><div id=bar><button data-b=left>Left</button> <button data-b=middle>Middle</button>
<button data-b=right>Right</button> <button data-b=long>Long press</button> <button data-b=stop>Stop server</button></div>
<p id=what></p><input id=up type=file multiple>
<p class=s>Nothing is ever overwritten: a file that differs from the one on the X is kept next to it as "name (web 1)".</p>
</div>
<script>
const $=id=>document.getElementById(id),H={'X-Writer':'1'};
let cfg={exts:[],max:0};
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
  const r=await api('/api/files');if(!r.ok){if(r.status!=401)say(r.j.error||'Failed',true);return}
  show(true);cfg=r.j;$('free').textContent=kb(r.j.free)+' free on the X';
  $('up').accept=r.j.exts.join(',');
  $('what').textContent='Add files ('+r.j.exts.join(' ')+', up to '+kb(r.j.max)+' each):';
  const box=$('files');box.textContent='';
  r.j.files.forEach(a=>{
    const d=document.createElement('div');d.className='row';
    const n=document.createElement('span');n.className='n';n.textContent=a.n;d.appendChild(n);
    const s=document.createElement('span');s.className='s';s.textContent=kb(a.s);d.appendChild(s);
    const x=document.createElement('button');x.textContent='Delete';x.onclick=async()=>{
      if(!confirm('Delete '+a.n+' from the X?'))return;
      const res=await api('/api/filedelete?name='+encodeURIComponent(a.n),{method:'POST'});
      say(res.ok?'Deleted.':(res.j.error||'Failed'),!res.ok);load()};d.appendChild(x);box.appendChild(d)});
  if(!r.j.files.length)box.textContent='No files yet.'}
$('go').onclick=async()=>{
  const r=await api('/api/login',{method:'POST',body:'pin='+encodeURIComponent($('pin').value),
    headers:Object.assign({'Content-Type':'application/x-www-form-urlencoded'},H)});
  if(r.ok){say('');load()}else say(r.j.error||'Failed',true)};
document.querySelectorAll('#bar button').forEach(b=>b.onclick=async()=>{
  const r=await api('/api/touch',{method:'POST',body:'b='+b.dataset.b,
    headers:Object.assign({'Content-Type':'application/x-www-form-urlencoded'},H)});
  say(r.ok?(b.dataset.b=='stop'||b.dataset.b=='long'?'Sent. The X stops this page.':'Sent.'):(r.j.error||'Failed'),!r.ok)});
$('pin').onkeydown=e=>{if(e.key=='Enter')$('go').click()};
$('up').onchange=async e=>{
  const out=[];
  for(const f of e.target.files){
    if(f.size>cfg.max){out.push(f.name+': file too big (largest '+kb(cfg.max)+')');continue}
    const r=await api('/api/file?name='+encodeURIComponent(f.name),{method:'POST',body:f});
    out.push(f.name+': '+(r.ok?(r.j.how=='same'?'already on the X':r.j.how=='both'?'kept both, saved as '+r.j.name:'saved'):(r.j.error||'failed')))}
  say(out.join('\n'));e.target.value='';load()};
load();
</script></body></html>)HTML";
