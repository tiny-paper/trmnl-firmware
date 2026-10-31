#pragma once

// The page of the settings profile (webshare_settings.cpp): the app's declared fields as a form, the keyboard layout,
// the touch bar's buttons. Labels, choices and values are shown with textContent / value only. A secret field starts
// empty and says "set" or "not set"; left empty it is kept.
static const char WEBSHARE_SETTINGS_PAGE[] = R"HTML(<!doctype html>
<html><head><meta charset=utf-8><meta name=viewport content="width=device-width,initial-scale=1">
<title>Settings</title>
<style>
body{font:16px/1.4 system-ui,sans-serif;max-width:42rem;margin:0 auto;padding:1rem;color:#111;background:#fff}
#pin{font-size:2.4rem;letter-spacing:.3em;width:7em;padding:.2em}#go{font-size:1.6rem;padding:.3em .8em}
h1{font-size:1.3rem}button,input,select{font:inherit;padding:.4rem .7rem;margin:.15rem 0;max-width:100%;box-sizing:border-box}
.f{display:block;margin:.8rem 0}.f input,.f select{width:100%}.s{color:#666;font-size:.85rem}
#msg{white-space:pre-line;margin:.6rem 0;padding:.5rem;background:#eef;border-radius:.3rem}#msg.bad{background:#fdd}
#kbd{margin:1rem 0;padding:.6rem 0;border-top:1px solid #ddd;border-bottom:1px solid #ddd}
@media(prefers-color-scheme:dark){body{background:#111;color:#eee}#msg{background:#224}#msg.bad{background:#522}#kbd{border-color:#444}}
</style></head><body>
<h1>Settings</h1>
<div id=msg hidden></div>
<div id=login><p>Type the 6-digit code shown on the X.</p>
<input id=pin inputmode=numeric pattern="[0-9]*" maxlength=6 autocomplete=off size=8> <button id=go>Open</button></div>
<div id=main hidden>
<form id=form autocomplete=off></form><button id=save>Save</button>
<div id=kbd></div>
<p>Touch bar:</p><div id=bar><button data-b=left>Left</button> <button data-b=middle>Middle</button>
<button data-b=right>Right</button> <button data-b=long>Long press</button> <button data-b=stop>Stop server</button></div>
</div>
<script>
const $=id=>document.getElementById(id),H={'X-Writer':'1'},F='application/x-www-form-urlencoded';
let fields=[];
function say(t,bad){const m=$('msg');m.textContent=t;m.className=bad?'bad':'';m.hidden=!t}
function show(ok){$('login').hidden=ok;$('main').hidden=!ok}
async function api(path,opt){
  try{const r=await fetch(path,Object.assign({headers:H,credentials:'same-origin'},opt||{}));
    if(r.status==401)show(false);
    let j={};try{j=await r.json()}catch(e){}
    return{ok:r.ok,status:r.status,j}}
  catch(e){return{ok:false,status:0,j:{error:'no connection (the X switched the page off?)'}}}}
function post(path,body){return api(path,{method:'POST',body,headers:Object.assign({'Content-Type':F},H)})}
async function load(){
  const r=await api('/api/settings');if(!r.ok){if(r.status!=401)say(r.j.error||'Failed',true);return}
  show(true);window.kbd&&kbd();fields=r.j.fields;const box=$('form');box.textContent='';
  fields.forEach(f=>{
    const l=document.createElement('label');l.className='f';
    const t=document.createElement('div');t.textContent=f.l;l.appendChild(t);let el;
    if(f.t=='choice'){el=document.createElement('select');
      f.choices.forEach(c=>{const o=document.createElement('option');o.value=c;o.textContent=c;o.selected=c==f.v;el.appendChild(o)})}
    else{el=document.createElement('input');
      if(f.t=='number'){el.type='number';el.min=f.min;el.max=f.max;el.step=1;el.inputMode='numeric'}
      else{el.type=f.t=='secret'?'password':'text';el.maxLength=f.max}
      if(f.t!='secret')el.value=f.v||''}
    el.name=f.k;l.appendChild(el);
    const h=document.createElement('div');h.className='s';
    h.textContent=f.t=='secret'?(f.set?'set; leave empty to keep it':'not set'):f.t=='number'?f.min+' to '+f.max:f.t=='text'?'up to '+f.max+' bytes':'';
    l.appendChild(h);box.appendChild(l)});
  if(!fields.length)box.textContent='This app has no settings.'}
$('save').onclick=async()=>{
  const p=new URLSearchParams();
  fields.forEach(f=>{const v=$('form').elements[f.k].value;if(f.t=='secret'&&v==='')return;p.append('v_'+f.k,v)});
  const r=await post('/api/settings',p.toString());
  say(r.ok?(r.j.changed?'Saved ('+r.j.changed+' changed).':'Nothing changed.'):(r.j.error||'Failed'),!r.ok);if(r.ok)load()};
$('go').onclick=async()=>{const r=await post('/api/login','pin='+encodeURIComponent($('pin').value));
  if(r.ok){say('');load()}else say(r.j.error||'Failed',true)};
document.querySelectorAll('#bar button').forEach(b=>b.onclick=async()=>{const r=await post('/api/touch','b='+b.dataset.b);
  say(r.ok?(b.dataset.b=='stop'||b.dataset.b=='long'?'Sent. The X stops this page.':'Sent.'):(r.j.error||'Failed'),!r.ok)});
$('pin').onkeydown=e=>{if(e.key=='Enter')$('go').click()};
load();
</script><script src=/kbd.js></script></body></html>)HTML";
