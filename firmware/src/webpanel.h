#pragma once
// Web control panel served at http://deskbuddy.local/ (military HUD style).
static const char PANEL_HTML[] PROGMEM = R"HTML(<!doctype html>
<html lang="es"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>DeskBuddy // Control</title>
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=B612+Mono:wght@400;700&display=swap">
<style>
:root{--bg:#000;--p:#04120a;--g:#39ff88;--m:#22b86c;--d:#13633c;--f:#0a3a23;--a:#ffb020;--r:#ff453a;color-scheme:dark}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--g);font:14px/1.5 "B612 Mono",ui-monospace,Consolas,monospace;
 padding:18px max(16px,env(safe-area-inset-left)) 90px;background-image:repeating-linear-gradient(0deg,rgba(0,0,0,.25) 0 1px,transparent 1px 3px)}
main{max-width:760px;margin:0 auto;display:grid;gap:18px}
header{display:grid;gap:6px;border-bottom:1px solid var(--d);padding-bottom:14px}
h1{margin:0;font-size:22px;letter-spacing:.12em;text-shadow:0 0 12px rgba(57,255,136,.45)}
.st{display:flex;flex-wrap:wrap;gap:6px 16px;font-size:12px;color:var(--m)}
.st b{color:var(--g)} .st .bad{color:var(--a)}
section{position:relative;background:var(--p);border:1px solid var(--f);padding:16px 16px 14px;display:grid;gap:12px}
section:before,section:after{content:"";position:absolute;width:12px;height:12px;border:2px solid var(--g)}
section:before{top:-1px;left:-1px;border-right:0;border-bottom:0} section:after{bottom:-1px;right:-1px;border-left:0;border-top:0}
h2{margin:0;font-size:13px;letter-spacing:.2em;color:var(--m)}
h2:before{content:"// ";color:var(--d)}
.row{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,1fr));gap:12px}
label{display:grid;gap:5px;font-size:11px;letter-spacing:.12em;color:var(--m)}
input,select{font:inherit;color:var(--g);background:#000;border:1px solid var(--d);padding:9px 10px;border-radius:2px;width:100%}
input:focus,select:focus,button:focus-visible{outline:2px solid var(--a);outline-offset:1px}
input[type=range]{padding:0;accent-color:var(--g)}
.tg{display:flex;align-items:center;gap:10px;font-size:12px;letter-spacing:.1em;color:var(--g);cursor:pointer}
.tg input{width:20px;height:20px;accent-color:var(--g)}
button,.btn{font:700 12px/1 inherit;font-family:inherit;letter-spacing:.14em;color:var(--g);background:#000;border:1px solid var(--g);
 padding:11px 14px;cursor:pointer;text-decoration:none;display:inline-block;border-radius:2px}
button:hover,.btn:hover{background:var(--f)} .warn{color:var(--a);border-color:var(--a)}
.res{display:grid;gap:4px} .res button{text-align:left;font-weight:400;border-color:var(--d)}
.val{color:var(--g);font-weight:700}
.bar{position:fixed;left:0;right:0;bottom:0;padding:12px max(16px,env(safe-area-inset-left)) calc(12px + env(safe-area-inset-bottom));
 background:rgba(0,0,0,.92);border-top:1px solid var(--d);display:flex;gap:12px;align-items:center;justify-content:center}
#msg{font-size:12px;color:var(--m);min-width:12ch}
.hint{font-size:11px;color:var(--d)}
</style></head><body><main>
<header><h1>DESKBUDDY // CONTROL</h1>
<div class="st" id="st">CONECTANDO...</div></header>

<section><h2>UBICACIÓN</h2>
<div class="row"><label>BUSCAR CIUDAD<input id="q" placeholder="Madrid"></label>
<div style="align-self:end"><button type="button" id="find">BUSCAR</button></div></div>
<div class="res" id="res"></div>
<div class="row"><label>LATITUD<input id="lat" type="number" step="0.0001"></label>
<label>LONGITUD<input id="lon" type="number" step="0.0001"></label>
<label>CIUDAD<input id="city" maxlength="31"></label></div>
<div class="hint">Centro del radar y del tiempo del reloj.</div></section>

<section><h2>RADAR</h2>
<label class="tg"><input type="checkbox" id="radarOn">RADAR DE AVIONES CERCANOS</label>
<label>RADIO <span class="val" id="rkv"></span><input type="range" id="rk" min="2" max="50"></label>
<label class="tg"><input type="checkbox" id="auto">SALTAR AL RADAR CUANDO ENTRA UN AVIÓN</label></section>

<section><h2>ALERTAS MUNDIALES</h2>
<label class="tg"><input type="checkbox" id="spotOn">EMERGENCIAS Y AERONAVES DESTACADAS</label>
<div class="row"><label>DURACIÓN (S)<input id="secs" type="number" min="10" max="600"></label>
<label>NO REPETIR EN (MIN)<input id="rep" type="number" min="5" max="1440"></label></div>
<label>INDICATIVOS A VIGILAR<input id="wcs" placeholder="AF1,AF2,EXEC1F"></label>
<label>TIPOS DE AERONAVE<input id="wty" placeholder="A337,BLCF,A124"></label></section>

<section><h2>PANTALLA</h2>
<label>BRILLO <span class="val" id="brv"></span><input type="range" id="br" min="10" max="255"></label>
<div class="row"><label>MODO NOCHE DESDE<select id="ns"></select></label>
<label>HASTA<select id="ne"></select></label></div>
<label>BRILLO NOCTURNO MÁX <span class="val" id="nmv"></span><input type="range" id="nm" min="0" max="255"></label>
<label class="tg"><input type="checkbox" id="eyes">OJOS CUANDO NO USAS EL PC</label>
<div class="row"><label>OJOS TRAS (MIN)<input id="idle" type="number" min="1" max="60"></label>
<label>DORMIR TRAS (MIN)<input id="sleep" type="number" min="1" max="120"></label></div></section>

<section><h2>SISTEMA</h2>
<div class="st" id="sys"></div>
<div style="display:flex;flex-wrap:wrap;gap:10px">
<a class="btn" href="/update">ACTUALIZAR FIRMWARE</a>
<button type="button" id="reboot">REINICIAR</button>
<button type="button" class="warn" id="wifi">CAMBIAR WIFI</button></div>
<div class="hint">Cambiar WiFi borra la red guardada: DeskBuddy abrirá la red DeskBuddy-Setup.</div></section>
</main>
<div class="bar"><button type="button" id="save">TRANSMITIR</button><span id="msg"></span></div>
<script>
const $=id=>document.getElementById(id),msg=t=>{$("msg").textContent=t};
for(let h=0;h<24;h++){for(const s of ["ns","ne"]){const o=document.createElement("option");o.value=h;o.textContent=String(h).padStart(2,"0")+":00";$(s).append(o)}}
const live=[["rk","rkv"," KM"],["br","brv",""],["nm","nmv",""]];
live.forEach(([i,o,u])=>$(i).addEventListener("input",()=>$(o).textContent=$(i).value+u));
async function load(){const c=await (await fetch("/api/config")).json();
 $("lat").value=c.home.lat.toFixed(4);$("lon").value=c.home.lon.toFixed(4);$("city").value=c.home.city;
 $("radarOn").checked=c.radar.enabled;$("rk").value=c.radar.radius_km;$("auto").checked=c.radar.auto_show;
 $("spotOn").checked=c.spotlight.enabled;$("secs").value=c.spotlight.show_seconds;$("rep").value=c.spotlight.repeat_minutes;
 $("wcs").value=c.spotlight.watch_callsigns.join(",");$("wty").value=c.spotlight.watch_types.join(",");
 const s=c.screen;$("ns").value=s.night_start;$("ne").value=s.night_end;$("nm").value=s.night_max;$("br").value=s.brightness;
 $("eyes").checked=s.eyes;$("idle").value=Math.round(s.idle_eye_s/60);$("sleep").value=Math.round(s.sleepy_s/60);
 live.forEach(([i,o,u])=>$(o).textContent=$(i).value+u)}
async function status(){try{const s=await (await fetch("/api/status")).json();
 $("st").innerHTML=`WIFI <b>${s.ssid}</b> ${s.rssi} dBm · PC <b class="${s.pc?"":"bad"}">${s.pc?s.host:"SIN ENLACE"}</b> · UP <b>${s.up}</b>`;
 $("sys").innerHTML=`FW <b>${s.fw}</b> · IP <b>${s.ip}</b> · RAM LIBRE <b>${(s.heap/1024).toFixed(0)} KB</b>`}catch(e){$("st").innerHTML='<span class="bad">SIN CONEXIÓN</span>'}}
const list=v=>v.split(/[\s,]+/).map(x=>x.trim().toUpperCase()).filter(Boolean);
$("save").onclick=async()=>{msg("TRANSMITIENDO...");
 const body={home:{lat:+$("lat").value,lon:+$("lon").value,city:$("city").value.trim()},
  radar:{enabled:$("radarOn").checked,radius_km:+$("rk").value,auto_show:$("auto").checked},
  spotlight:{enabled:$("spotOn").checked,show_seconds:+$("secs").value,repeat_minutes:+$("rep").value,watch_callsigns:list($("wcs").value),watch_types:list($("wty").value)},
  screen:{night_start:+$("ns").value,night_end:+$("ne").value,night_max:+$("nm").value,brightness:+$("br").value,eyes:$("eyes").checked,idle_eye_s:+$("idle").value*60,sleepy_s:+$("sleep").value*60}};
 const r=await fetch("/api/config",{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(body)});
 msg(r.ok?"GUARDADO ✓":"ERROR "+r.status);if(r.ok)load()};
$("find").onclick=async()=>{const q=$("q").value.trim();if(!q)return;$("res").textContent="BUSCANDO...";
 try{const j=await (await fetch("https://geocoding-api.open-meteo.com/v1/search?count=5&language=es&name="+encodeURIComponent(q))).json();
 $("res").textContent="";(j.results||[]).forEach(p=>{const b=document.createElement("button");b.type="button";
 b.textContent=`${p.name} · ${p.admin1||""} · ${p.country||""}  (${p.latitude.toFixed(3)}, ${p.longitude.toFixed(3)})`;
 b.onclick=()=>{$("lat").value=p.latitude.toFixed(4);$("lon").value=p.longitude.toFixed(4);$("city").value=p.name;$("res").textContent="";msg("PULSA TRANSMITIR")};$("res").append(b)});
 if(!(j.results||[]).length)$("res").textContent="SIN RESULTADOS"}catch(e){$("res").textContent="NO SE PUDO BUSCAR (¿SIN INTERNET?)"}};
$("q").addEventListener("keydown",e=>{if(e.key==="Enter"){e.preventDefault();$("find").click()}});
$("reboot").onclick=async()=>{if(!confirm("¿Reiniciar DeskBuddy?"))return;await fetch("/api/reboot",{method:"POST"});msg("REINICIANDO...")};
$("wifi").onclick=async()=>{if(!confirm("¿Olvidar la red WiFi? DeskBuddy abrirá DeskBuddy-Setup."))return;await fetch("/api/wifireset",{method:"POST"});msg("WIFI BORRADO")};
load();status();setInterval(status,5000);
</script></body></html>)HTML";
