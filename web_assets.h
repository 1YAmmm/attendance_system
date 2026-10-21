// web_assets.h - stylesheet and dashboard script for the web pages.
// Kept in a separate file because the Arduino IDE misreads code inside long text blocks in the .ino.
#pragma once
#include <pgmspace.h>

// Shared stylesheet (served once at /app.css, then cached by the browser)
const char APP_CSS[] PROGMEM = R"css(
:root{--blue:#007BFF;--blue-d:#0062CC;--blue-l:#EAF3FF;--ink:#0F172A;--muted:#64748B;--line:#E2E8F0;--bg:#F5F8FC;--card:#FFFFFF}
*{box-sizing:border-box}
html{-webkit-text-size-adjust:100%}
body{margin:0;font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;background:var(--bg);color:var(--ink);font-size:15px;line-height:1.5}
a{color:var(--blue);text-decoration:none}
svg{flex:none}
.nav{position:sticky;top:0;z-index:10;background:rgba(255,255,255,.9);-webkit-backdrop-filter:blur(10px);backdrop-filter:blur(10px);border-bottom:1px solid var(--line)}
.nav-in{max-width:1100px;margin:0 auto;display:flex;align-items:center;flex-wrap:wrap;gap:12px;padding:12px 16px}
.brand{display:flex;align-items:center;gap:10px;font-weight:800;color:var(--ink);letter-spacing:-.01em}
.brand small{display:block;font-weight:500;color:var(--muted);font-size:12px;letter-spacing:0}
.logo{width:36px;height:36px;border-radius:11px;background:linear-gradient(135deg,#007BFF,#38BDF8);color:#fff;display:inline-grid;place-items:center;box-shadow:0 6px 14px rgba(0,123,255,.3)}
.logo.big{width:56px;height:56px;border-radius:16px;margin-bottom:10px}
.links{margin-left:auto;display:flex;gap:4px}
.links a{padding:8px 14px;border-radius:999px;color:var(--muted);font-weight:600;font-size:14px}
.links a:hover,.links a.on{background:var(--blue-l);color:var(--blue)}
.wrap{max-width:1100px;margin:0 auto;padding:28px 16px 48px}
.narrow{max-width:560px}
.hero{display:flex;flex-wrap:wrap;align-items:flex-end;justify-content:space-between;gap:16px;margin-bottom:20px}
.eyebrow{display:inline-block;font-size:12px;font-weight:700;letter-spacing:.06em;text-transform:uppercase;color:var(--blue);background:var(--blue-l);padding:5px 12px;border-radius:999px}
h1{font-size:30px;line-height:1.15;letter-spacing:-.02em;margin:12px 0 6px}
h2{font-size:20px;letter-spacing:-.01em;margin:0 0 4px}
.sub{color:var(--muted);margin:0}
.btns{display:flex;flex-wrap:wrap;gap:8px}
.btn{display:inline-flex;align-items:center;justify-content:center;gap:8px;border:1px solid transparent;cursor:pointer;font:inherit;font-weight:700;font-size:14px;padding:11px 18px;border-radius:12px;background:var(--blue);color:#fff;box-shadow:0 8px 18px rgba(0,123,255,.25);transition:background .15s,box-shadow .15s}
.btn:hover{background:var(--blue-d)}
.btn.ghost{background:#fff;color:var(--ink);border-color:var(--line);box-shadow:none}
.btn.ghost:hover{border-color:var(--blue);color:var(--blue)}
.btn.block{display:flex;width:100%;margin-top:20px}
.btn.ghost.block{margin-top:10px}
.tiles{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:12px;margin-bottom:20px}
.tile{background:var(--card);border:1px solid var(--line);border-radius:16px;padding:16px 18px;box-shadow:0 1px 2px rgba(15,23,42,.04)}
.k{display:flex;align-items:center;gap:8px;font-size:13px;font-weight:600;color:var(--muted)}
.dot{width:9px;height:9px;border-radius:50%;display:inline-block}
.v{font-size:32px;font-weight:800;letter-spacing:-.02em;margin-top:4px}
.card{background:var(--card);border:1px solid var(--line);border-radius:20px;box-shadow:0 12px 32px rgba(15,23,42,.06);overflow:hidden}
.pad{padding:24px}
.card-h{display:flex;align-items:center;justify-content:space-between;gap:12px;padding:16px 20px;border-bottom:1px solid var(--line)}
.card-h h2{font-size:17px;margin:0}
.live{display:inline-flex;align-items:center;gap:6px;font-size:12px;font-weight:700;color:#16A34A}
.live:before{content:"";width:8px;height:8px;border-radius:50%;background:currentColor;animation:blink 1.6s infinite}
.live.off{color:var(--muted)}
.live.off:before{animation:none}
@keyframes blink{50%{opacity:.25}}
.tbl{overflow-x:auto}
table{width:100%;border-collapse:collapse;min-width:720px}
th{font-size:11px;font-weight:700;letter-spacing:.06em;text-transform:uppercase;color:var(--muted);text-align:left;padding:12px 20px;background:#F8FAFC}
td{padding:12px 20px;border-top:1px solid var(--line);vertical-align:middle}
td small{display:block;color:var(--muted);font-size:12px;margin-top:3px}
.num{color:var(--muted);width:44px}
.nm{font-weight:700}
.pill{display:inline-block;font-size:12px;font-weight:700;padding:3px 10px;border-radius:999px;background:#F1F5F9;color:var(--muted);white-space:nowrap}
.pill.ok{background:#DCFCE7;color:#15803D}
.pill.late{background:#FEF3C7;color:#B45309}
.pill.abs{background:#FEE2E2;color:#B91C1C}
.pill.out{background:var(--blue-l);color:var(--blue-d)}
.empty{text-align:center;color:var(--muted);padding:36px 20px}
.alert{border:1px solid #FDE68A;background:#FFFBEB;color:#92400E;border-radius:12px;padding:12px 14px;margin-bottom:12px;font-size:14px}
.alert.err{border-color:#FECACA;background:#FEF2F2;color:#991B1B}
.alert.ok{border-color:#BBF7D0;background:#F0FDF4;color:#166534}
.alert a{font-weight:700}
label{display:block;font-size:13px;font-weight:600;color:var(--muted);margin-top:16px}
label small{font-weight:500}
input{display:block;width:100%;margin-top:6px;padding:12px 14px;font:inherit;color:var(--ink);background:#fff;border:1px solid var(--line);border-radius:12px;transition:border-color .15s,box-shadow .15s}
input:focus{outline:none;border-color:var(--blue);box-shadow:0 0 0 4px rgba(0,123,255,.15)}
.status{background:#F8FAFC;border:1px solid var(--line);border-radius:12px;padding:12px 14px;font-size:14px;margin-top:12px}
.status small{color:var(--muted)}
.hint{font-size:13px;color:var(--muted);margin:16px 0 0}
.menu{display:grid;gap:12px;margin-top:20px}
.mi{display:flex;align-items:center;gap:14px;padding:16px 18px;background:#fff;border:1px solid var(--line);border-radius:16px;color:var(--ink);transition:border-color .15s,box-shadow .15s}
.mi:hover{border-color:var(--blue);box-shadow:0 8px 20px rgba(0,123,255,.12)}
.mi:after{content:"\203A";margin-left:auto;font-size:24px;line-height:1;color:var(--muted)}
.ic{width:44px;height:44px;border-radius:12px;background:var(--blue-l);color:var(--blue);display:grid;place-items:center;flex:none}
.nets{display:grid;gap:8px;margin-top:8px}
.net{display:flex;align-items:center;justify-content:space-between;gap:10px;width:100%;padding:12px 14px;font:inherit;color:var(--ink);background:#fff;border:1px solid var(--line);border-radius:12px;cursor:pointer;text-align:left}
.net:hover{border-color:var(--blue)}
.net b{overflow:hidden;text-overflow:ellipsis;white-space:nowrap}
.net small{color:var(--muted);font-size:12px;white-space:nowrap}
.mi b{display:block}
.mi small{display:block;color:var(--muted);font-size:13px}
.login{min-height:100vh;display:flex;align-items:center;justify-content:center;padding:16px;background:radial-gradient(900px 420px at 50% -8%,#DBEAFE,rgba(219,234,254,0)),var(--bg)}
.login .card{width:100%;max-width:400px}
.center{text-align:center}
.foot{color:var(--muted);font-size:12px;text-align:center;margin:20px 0 0}
@media(max-width:760px){.tiles{grid-template-columns:repeat(2,minmax(0,1fr))}h1{font-size:25px}}
@media(max-width:520px){.links{margin-left:0;width:100%}.links a{flex:1;text-align:center;padding:8px 6px}.btns .btn{flex:1 1 100%}.pad{padding:20px}}
)css";

// Dashboard script: draws the table and refreshes it every 5 seconds
const char DASH_JS[] PROGMEM = R"js(
function E(s){return String(s).replace(/[&<>"]/g,function(c){return{'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]})}
function C(s){return s=='On Time'?'ok':(s=='Late In'||s=='No Time Out')?'late':s=='Absent'?'abs':s=='Timed Out'?'out':''}
function cell(s,t){return '<td><span class="pill '+C(s)+'">'+E(s)+'</span><small>'+E(t)+'</small></td>'}
function R(d){
 document.getElementById('tP').textContent=d.present;
 document.getElementById('tL').textContent=d.late;
 document.getElementById('tA').textContent=d.absent;
 document.getElementById('tN').textContent=d.pending;
 document.getElementById('dt').textContent='Today · '+d.date;
 document.getElementById('tm').textContent='Last update '+d.time;
 var h='';
 if(!d.students.length)h='<tr><td colspan="6" class="empty">No students yet. Add them with /addstudent in Telegram.</td></tr>';
 d.students.forEach(function(s,i){var a=s.s;h+='<tr><td class="num">'+(i+1)+'</td><td><span class="nm">'+E(s.n)+'</span><small>LRN '+E(s.l)+'</small></td>'+cell(a[0],a[1])+cell(a[2],a[3])+cell(a[4],a[5])+cell(a[6],a[7])+'</tr>'});
 document.getElementById('rows').innerHTML=h;
}
function T(){fetch('/api/today',{cache:'no-store'}).then(function(r){if(r.status==401){location='/login';throw 0}return r.json()}).then(function(d){R(d);document.getElementById('live').className='live'}).catch(function(){document.getElementById('live').className='live off'})}
R(INIT);setInterval(T,5000);
)js";

// WiFi page script: lists the nearby networks (the scan runs in the background, so it
// asks /api/networks again until the list is ready); tapping one fills in the WiFi name
const char WIFI_JS[] PROGMEM = R"js(
function $(i){return document.getElementById(i)}
function pickNet(b){$('ssid').value=b.getAttribute('data-n');$('open').value=b.getAttribute('data-open');$('pass').value='';$('pass').focus()}
function netMsg(t){$('nets').innerHTML='<div class="status">'+t+'</div>'}
function listNets(list){
 var box=$('nets');box.innerHTML='';
 if(!list.length){netMsg('No WiFi found. Check that the router is on, then tap refresh.');return}
 list.forEach(function(n){
  var b=document.createElement('button'),nm=document.createElement('b'),sm=document.createElement('small');
  b.type='button';b.className='net';b.setAttribute('data-n',n.s);b.setAttribute('data-open',n.o?'1':'0');
  b.onclick=function(){pickNet(b)};
  nm.textContent=n.s;sm.textContent=(n.r>-60?'Strong':n.r>-75?'Good':'Weak')+(n.o?' · no password':'');
  b.appendChild(nm);b.appendChild(sm);box.appendChild(b)})
}
function askNets(start,tries){
 fetch('/api/networks'+(start?'?start=1':''),{cache:'no-store'}).then(function(r){
  if(r.status==401){location='/login?next=/settings/wifi';throw 0}return r.json()
 }).then(function(d){
  if(d.nets)listNets(d.nets);
  else if(tries<25)setTimeout(function(){askNets(d.busy,tries+1)},1000);
  else netMsg('The device is busy. Tap refresh to try again.')
 }).catch(function(e){if(e!==0&&tries<25)setTimeout(function(){askNets(start,tries+1)},1000)})
}
function scanNets(){netMsg('Searching for WiFi...');askNets(1,0)}
$('ssid').oninput=function(){$('open').value='0'};
scanNets();
)js";
