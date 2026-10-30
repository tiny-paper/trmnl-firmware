#pragma once

// The page of the writer's Wi-Fi page (webshare.cpp): one HTML file, no libraries. File names are shown with
// textContent only (they come from users and uploads), never as HTML.
static const char WEBSHARE_PAGE[] = R"HTML(<!doctype html>
<html lang=en><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Writer files</title>
<style>
body{font:16px/1.4 system-ui,sans-serif;max-width:42rem;margin:0 auto;padding:1rem;color:#111;background:#fff}
h1{font-size:1.3rem}h2{font-size:1.05rem;margin:1.2rem 0 .3rem}
button,input{font:inherit;padding:.4rem .7rem;margin:.15rem 0}
.row{display:flex;gap:.5rem;align-items:center;border-bottom:1px solid #ddd;padding:.3rem 0;flex-wrap:wrap}
.row .n{flex:1;min-width:10rem;word-break:break-all}.s{color:#666;font-size:.85rem}
#msg{white-space:pre-line;margin:.6rem 0;padding:.5rem;background:#eef;border-radius:.3rem}#msg.bad{background:#fdd}
@media(prefers-color-scheme:dark){body{background:#111;color:#eee}.row{border-color:#444}#msg{background:#224}#msg.bad{background:#522}}
</style></head><body>
<h1>Writer files</h1>
<div id=msg hidden></div>
<div id=login>
<p>Type the 6-digit code shown on the X.</p>
<input id=pin inputmode=numeric pattern="[0-9]*" maxlength=6 autocomplete=off size=8> <button id=go>Open</button>
</div>
<div id=main hidden>
<div class=row><button id=zip>Download all as zip</button><span class=s id=free></span></div>
<h2>Documents</h2><div id=docs></div>
<h2>Add documents</h2>
<p class=s>Folder: <input id=folder size=14 placeholder="Notes"> then choose .md or .txt files.</p>
<input id=up type=file multiple accept=".md,.txt">
<h2>Fonts</h2><div id=fonts></div>
<p class=s>Add a font (.ttf or .otf):</p><input id=font type=file accept=".ttf,.otf">
<p class=s>Nothing is ever overwritten: a file that differs from the one on the X is kept next to it as "name (web 1)".</p>
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
function row(parent,path,size,doc){
  const d=document.createElement('div');d.className='row';
  const n=document.createElement('span');n.className='n';n.textContent=path.split('/').slice(1).join(' / ');d.appendChild(n);
  const s=document.createElement('span');s.className='s';s.textContent=kb(size);d.appendChild(s);
  if(doc){
    const a=document.createElement('a');a.href='/api/get?p='+encodeURIComponent(path);a.textContent='Download';a.download='';d.appendChild(a);
    const r=document.createElement('button');r.textContent='Rename';r.onclick=async()=>{
      const cur=path.split('/');const nn=prompt('New name (without folder)',cur[2]);if(!nn||nn==cur[2])return;
      const res=await api('/api/rename?from='+encodeURIComponent(path)+'&to='+encodeURIComponent(cur[0]+'/'+cur[1]+'/'+nn),{method:'POST'});
      say(res.ok?'Renamed.':(res.j.error||'Failed'),!res.ok);load()};d.appendChild(r);
    const x=document.createElement('button');x.textContent='Delete';x.onclick=async()=>{
      if(!confirm('Delete '+path+' from the X?'))return;
      const res=await api('/api/delete?p='+encodeURIComponent(path),{method:'POST'});
      say(res.ok?'Deleted.':(res.j.error||'Failed'),!res.ok);load()};d.appendChild(x)}
  parent.appendChild(d)}
async function load(){
  const r=await api('/api/list');if(!r.ok){if(r.status!=401)say(r.j.error||'Failed',true);return}
  show(true);$('free').textContent=kb(r.j.free)+' free';
  const dd=$('docs'),ff=$('fonts');dd.textContent='';ff.textContent='';
  r.j.docs.forEach(f=>row(dd,f.p,f.s,true));if(!r.j.docs.length)dd.textContent='No documents.';
  r.j.fonts.forEach(f=>row(ff,f.p,f.s,false))}
async function put(path,file){
  const r=await api('/api/put?p='+encodeURIComponent(path),{method:'POST',body:file});
  if(!r.ok)return file.name+': '+(r.j.error||'failed');
  return file.name+': '+(r.j.how=='same'?'already on the X':r.j.how=='both'?'kept both, saved as '+r.j.path:'saved')}
$('go').onclick=async()=>{
  const r=await api('/api/login',{method:'POST',body:'pin='+encodeURIComponent($('pin').value),
    headers:Object.assign({'Content-Type':'application/x-www-form-urlencoded'},H)});
  if(r.ok){say('');load()}else say(r.j.error||'Failed',true)};
$('pin').onkeydown=e=>{if(e.key=='Enter')$('go').click()};
$('zip').onclick=()=>{location.href='/api/zip'};
$('up').onchange=async e=>{
  const folder=$('folder').value.trim()||'Web';const out=[];
  for(const f of e.target.files)out.push(await put('documents/'+folder+'/'+f.name,f));
  say(out.join('\n'));e.target.value='';load()};
$('font').onchange=async e=>{
  const f=e.target.files[0];if(!f)return;say(await put('settings/fonts/'+f.name,f));e.target.value='';load()};
load();
</script></body></html>)HTML";
