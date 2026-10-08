#pragma once
#include <pgmspace.h>

// Single-page dashboard served at "/". Polls /api/live every second and
// /api/files every 5 s. No external resources, so it works on the car's
// offline hotspot.
static const char UI_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>OBD Logger</title>
<style>
:root{--bg:#0e1319;--card:#171e27;--line:#26303c;--fg:#e6edf3;--dim:#8b98a5;--teal:#2dd8c4;--amber:#e8a548;--red:#ef5b5b}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;padding:14px;max-width:760px;margin:auto}
header{display:flex;justify-content:space-between;align-items:center;gap:10px;margin-bottom:12px}
h1{font-size:18px;margin:0}
.hright{display:flex;align-items:center;gap:10px;flex-wrap:wrap;justify-content:flex-end}
.clock{color:var(--dim);font-size:13px;font-variant-numeric:tabular-nums}
.pill{padding:4px 10px;border-radius:99px;font-size:12px;font-weight:600;background:#2a3441;color:var(--dim)}
.pill.ok{background:#12352f;color:var(--teal)}.pill.bad{background:#3b1f1f;color:var(--red)}
.banner{display:none;margin-bottom:12px;padding:10px 12px;border-radius:10px;font-weight:600}
.banner.regen{display:block;background:#3a2b10;color:var(--amber);border:1px solid #6b4d17}
.banner.idle{display:block;background:#22303a;color:var(--dim);font-weight:500}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px}
.tile{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:10px 12px}
.tile.warn{border-color:var(--amber)}
.lbl{font-size:12px;color:var(--dim)}
.val{font-size:26px;font-weight:650;font-variant-numeric:tabular-nums}
.val small{font-size:13px;color:var(--dim);font-weight:500;margin-left:4px}
.val.na{color:#4a5563}
h2{font-size:14px;margin:18px 0 8px;color:var(--dim);font-weight:600;letter-spacing:.04em;text-transform:uppercase}
canvas{width:100%;height:90px;background:var(--card);border:1px solid var(--line);border-radius:12px;display:block}
.charts{display:grid;grid-template-columns:1fr;gap:10px}
table{width:100%;border-collapse:collapse}
td{padding:9px 6px;border-bottom:1px solid var(--line)}
td.r{text-align:right;color:var(--dim);white-space:nowrap}
a{color:var(--teal);text-decoration:none}
.foot{margin-top:14px;color:var(--dim);font-size:12px}
</style></head><body>
<header><h1>OBD Logger</h1><span class="hright"><span id="clock" class="clock"></span><span id="pill" class="pill">connecting…</span></span></header>
<div id="banner" class="banner"></div>
<div id="grid" class="grid"></div>
<h2>Last 2 minutes</h2>
<div class="charts">
<div><div class="lbl">DPF pressure (hPa)</div><canvas id="c1" width="700" height="90"></canvas></div>
<div><div class="lbl">Exhaust temp before DPF (°C)</div><canvas id="c2" width="700" height="90"></canvas></div>
<div><div class="lbl">RPM</div><canvas id="c3" width="700" height="90"></canvas></div>
</div>
<h2>Log files</h2>
<table id="files"><tr><td>loading…</td></tr></table>
<div class="foot" id="foot"></div>
<script>
const TILES=[["engine_rpm","RPM","",0],["vehicle_speed","Speed","km/h",0],["coolant_temp","Coolant","°C",0],
["engine_load_pct","Load","%",0],["dpf_diff_pressure_hpa","DPF pressure","hPa",1],["dpf_soot_level_g","Soot","g",1],
["egt_before_dpf_c","EGT before DPF","°C",0],["dpf_zone_temp_c","Catalyst temp","°C",0],
["dpf_dist_since_regen_mi","Since last regen","mi",1],["intercooler_temp_c","Intercooler","°C",0],
["maf_flow","MAF","g/s",1],["control_module_v","Battery (ECU)","V",2],["eps_voltage_v","Battery (EPS)","V",1],["egr_duty_pct","EGR","%",0]];
const grid=document.getElementById('grid');
TILES.forEach(t=>{const d=document.createElement('div');d.className='tile';d.id='t_'+t[0];
d.innerHTML='<div class="lbl">'+t[1]+'</div><div class="val na">—</div>';grid.appendChild(d);});
const hist={dp:[],egt:[],rpm:[]};const N=120;
function push(a,v){a.push(v);if(a.length>N)a.shift();}
function draw(id,a,col){const c=document.getElementById(id),x=c.getContext('2d');x.clearRect(0,0,c.width,c.height);
const v=a.filter(z=>z!==null);if(v.length<2)return;let lo=Math.min(...v),hi=Math.max(...v);if(hi-lo<1){hi=lo+1;}
x.strokeStyle=col;x.lineWidth=2;x.beginPath();let first=true;
a.forEach((y,i)=>{if(y===null){first=true;return;}const px=i*(c.width/(N-1)),py=c.height-6-(y-lo)/(hi-lo)*(c.height-14);
if(first){x.moveTo(px,py);first=false}else x.lineTo(px,py)});x.stroke();
x.fillStyle='#8b98a5';x.font='11px sans-serif';x.fillText(hi.toFixed(0),4,11);x.fillText(lo.toFixed(0),4,c.height-3);}
function fmt(v,d){return v===null||v===undefined?null:v.toFixed(d);}
async function live(){
 try{const r=await fetch('/api/live',{cache:'no-store'});const j=await r.json();const v=j.values;
 document.getElementById('clock').textContent=j.local||'clock not set';
 const p=document.getElementById('pill');
 if(!j.link){p.textContent='adapter not connected';p.className='pill bad';}
 else if(v.engine_rpm===null){p.textContent='adapter ok · engine off';p.className='pill';}
 else{p.textContent='live';p.className='pill ok';}
 TILES.forEach(t=>{const el=document.querySelector('#t_'+t[0]+' .val'),f=fmt(v[t[0]],t[3]);
  if(f===null){el.className='val na';el.textContent='—';}else{el.className='val';el.innerHTML=f+(t[2]?'<small>'+t[2]+'</small>':'');}});
 document.getElementById('t_egt_before_dpf_c').classList.toggle('warn',v.egt_before_dpf_c!==null&&v.egt_before_dpf_c>=300);
 const b=document.getElementById('banner');b.className='banner';b.textContent='';
 if(v.dpf_regen_active===1){b.className='banner regen';b.textContent=v.dpf_regen_burning===1?'DPF regeneration in progress — burning':'DPF regeneration in progress — heating up';}
 else if(v.engine_rpm!==null&&v.engine_rpm<=1100&&v.dpf_diff_pressure_hpa!==null&&v.dpf_diff_pressure_hpa>=20){b.className='banner idle';b.textContent='Idle DPF pressure at or above 20 hPa — compare against a hot-engine reading';}
 push(hist.dp,v.dpf_diff_pressure_hpa);push(hist.egt,v.egt_before_dpf_c);push(hist.rpm,v.engine_rpm);
 draw('c1',hist.dp,'#2dd8c4');draw('c2',hist.egt,'#e8a548');draw('c3',hist.rpm,'#8ab4ff');
 document.getElementById('foot').textContent='session '+j.session+' · '+j.storage+' · '+(j.free_mb>=0?j.free_mb+' MB free':'')+' · up '+Math.round(j.t/1000)+'s'+(j.home?' · home '+j.home:'')+(j.time?' · clock: '+j.time:' · clock not set');
 }catch(e){const p=document.getElementById('pill');p.textContent='no link to logger';p.className='pill bad';}
}
function human(n){return n>1048576?(n/1048576).toFixed(1)+' MB':n>1024?(n/1024).toFixed(0)+' KB':n+' B';}
async function files(){
 try{const r=await fetch('/api/files',{cache:'no-store'});const j=await r.json();
 j.sort((a,b)=>(parseInt(b.name.replace(/\D/g,''))||0)-(parseInt(a.name.replace(/\D/g,''))||0)||a.name.localeCompare(b.name));
 const t=document.getElementById('files');
 t.innerHTML=j.length?j.map(f=>'<tr><td><a href="/dl?f='+encodeURIComponent(f.name)+'">'+f.name+'</a></td><td class="r">'+human(f.size)+'</td></tr>').join(''):'<tr><td>no files yet</td></tr>';
 }catch(e){}
}
fetch('/api/time?ms='+Date.now()).catch(()=>{});
live();files();setInterval(live,1000);setInterval(files,5000);
</script></body></html>)HTML";
