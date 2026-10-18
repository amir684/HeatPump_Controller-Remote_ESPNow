#pragma once
#include <pgmspace.h>

// Single-page UI served at "/". Polls /api/status every 2 s.
static const char WEB_PAGE[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="he" dir="rtl">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>משאבת חום</title>
<style>
:root{--bg:#f3f5f7;--card:#fff;--fg:#1d2329;--mut:#66717c;--acc:#0b6bcb;--ok:#1e8e3e;--bad:#c5221f;--warn:#b26a00;--line:#dde2e7}
@media (prefers-color-scheme:dark){:root{--bg:#14181c;--card:#1d2328;--fg:#e6eaee;--mut:#94a0ab;--acc:#5aa9ff;--ok:#5bc27a;--bad:#ff6b63;--warn:#f0a940;--line:#2e363d}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,-apple-system,"Segoe UI",Arial,sans-serif}
main{max-width:760px;margin:0 auto;padding:12px 16px 40px}
h1{font-size:20px;margin:6px 0 12px;display:flex;align-items:center;gap:10px;flex-wrap:wrap}
h2{font-size:16px;margin:0 0 10px}
.card{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:14px;margin-bottom:12px}
.pill{font-size:12px;padding:2px 8px;border-radius:99px;border:1px solid currentColor;font-weight:600}
.ok{color:var(--ok)}.bad{color:var(--bad)}.warn{color:var(--warn)}.mut{color:var(--mut)}
.temps{display:grid;grid-template-columns:repeat(auto-fit,minmax(130px,1fr));gap:10px}
.t{border:1px solid var(--line);border-radius:8px;padding:8px 10px}
.t b{display:block;font-size:24px;font-variant-numeric:tabular-nums}
.t span{color:var(--mut);font-size:13px}
.relays{display:flex;gap:8px;flex-wrap:wrap;margin-top:10px}
.relay{padding:3px 10px;border-radius:6px;border:1px solid var(--line);color:var(--mut);font-weight:600;font-size:13px}
.relay.on{background:var(--ok);border-color:var(--ok);color:#fff}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}
button{font:inherit;padding:8px 14px;border-radius:8px;border:1px solid var(--line);background:var(--card);color:var(--fg);cursor:pointer}
button.pri{background:var(--acc);border-color:var(--acc);color:#fff}
button:disabled{opacity:.5;cursor:default}
.grid{display:grid;grid-template-columns:1fr 120px;gap:8px 12px;align-items:center}
.grid label{color:var(--fg)}
.grid small{display:block;color:var(--mut);font-size:12px}
input,select{font:inherit;width:100%;padding:6px 8px;border-radius:6px;border:1px solid var(--line);background:var(--bg);color:var(--fg)}
input[type=file]{padding:4px}
.msg{min-height:1.4em;margin-top:8px;font-size:14px}
progress{width:100%;height:14px;margin-top:8px}
.kv{display:grid;grid-template-columns:auto 1fr;gap:4px 14px;font-size:14px}
.kv span:nth-child(odd){color:var(--mut)}
details summary{cursor:pointer;font-weight:600}
@media (max-width:480px){.grid{grid-template-columns:1fr 100px}}
</style>
</head>
<body>
<main>
<h1>משאבת חום <span id="link" class="pill mut">…</span> <span id="err" class="pill bad" hidden></span></h1>

<section class="card">
  <div class="row" style="justify-content:space-between">
    <h2 style="margin:0">מצב: <span id="state">…</span></h2>
    <div class="row">
      <button id="btnPower" class="pri">…</button>
      <button id="btnDefrost">הפשרה יזומה</button>
    </div>
  </div>
  <div class="temps" style="margin-top:12px">
    <div class="t"><span>מים</span><b id="water">--</b></div>
    <div class="t"><span>יעד</span><b id="sp">--</b></div>
    <div class="t"><span>חוץ</span><b id="outdoor">--</b></div>
    <div class="t"><span>צנרת</span><b id="pipe">--</b></div>
    <div class="t"><span>פליטה</span><b id="exhaust">--</b></div>
  </div>
  <div class="relays">
    <span class="relay" id="rComp">מדחס</span>
    <span class="relay" id="rFan">מאוורר</span>
    <span class="relay" id="rValve">שסתום</span>
    <span class="relay" id="rDef">הפשרה</span>
  </div>
  <div class="msg mut" id="statusMsg"></div>
</section>

<section class="card">
  <h2>הגדרות</h2>
  <form id="fSet">
    <div class="grid" id="params"></div>
    <div class="row" style="margin-top:12px"><button class="pri" type="submit">שמור</button><button type="button" id="btnReload">טען מחדש</button></div>
    <div class="msg" id="setMsg"></div>
  </form>
</section>

<section class="card">
  <details>
    <summary>MQTT וסיסמה</summary>
    <form id="fNet" style="margin-top:10px">
      <div class="grid">
        <label>כתובת Broker</label><input name="mqttHost" maxlength="63">
        <label>פורט</label><input name="mqttPort" type="number" min="1" max="65535">
        <label>משתמש</label><input name="mqttUser" maxlength="31">
        <label>סיסמת MQTT<small>ריק = ללא שינוי</small></label><input name="mqttPass" type="password" maxlength="31">
        <label>סיסמת דף הניהול<small>ריק = ללא שינוי</small></label><input name="webPass" type="password" maxlength="31">
      </div>
      <div class="row" style="margin-top:12px"><button class="pri" type="submit">שמור</button></div>
      <div class="msg" id="netMsg"></div>
    </form>
  </details>
</section>

<section class="card">
  <h2>עדכון קושחה</h2>
  <div class="kv" style="margin-bottom:12px">
    <span>שלט</span><span id="fwRemote">--</span>
    <span>בקר חיצוני</span><span id="fwCtrl">--</span>
  </div>
  <div class="grid" style="grid-template-columns:1fr">
    <label>שלט (remote firmware.bin)</label>
    <div class="row"><input type="file" id="fileRemote" accept=".bin" style="flex:1"><button id="upRemote">עדכן שלט</button></div>
    <label style="margin-top:8px">בקר חיצוני (controller firmware.bin)<small>המדחס נעצר בזמן העדכון. ההעברה דרך ESP-NOW לוקחת כדקה.</small></label>
    <div class="row"><input type="file" id="fileCtrl" accept=".bin" style="flex:1"><button id="upCtrl">עדכן בקר</button></div>
  </div>
  <progress id="prog" max="100" value="0" hidden></progress>
  <div class="msg" id="fwMsg"></div>
</section>

<p class="mut" style="font-size:12px;text-align:center" id="foot"></p>
</main>

<script>
const $=id=>document.getElementById(id);
const STATES={offline:"כבוי",antifreeze_on:"הגנת קפיאה",antifreeze_wait:"הגנת קפיאה (המתנה)",startup_fan:"הפעלה — מאוורר",comp_delay:"המתנת מדחס",heating:"חימום",shutdown_comp:"כיבוי — מדחס",shutdown_fan:"כיבוי — מאוורר",standby:"המתנה",defrost_stop:"הפשרה — עצירה",defrost_valve:"הפשרה — שסתום",defrosting:"הפשרה",defrost_end_stop:"סיום הפשרה",defrost_end_valve:"סיום הפשרה — שסתום",exhaust_protect:"הגנת פליטה"};
const ERRORS={E01_water:"E01 חיישן מים",E02_pipe:"E02 חיישן צנרת",E03_exhaust:"E03 חיישן פליטה",E04_outdoor:"E04 חיישן חוץ",EH_exhaust_alarm:"EH טמפ' פליטה גבוהה"};
const PARAMS=[
 ["sp","טמפרטורת יעד (°C)",10,70,1],
 ["u03","U03 מקסימום יעד (°C)",30,70,1],
 ["u01","U01 היסטרזיס (°C)",2,10,0.5],
 ["u02","U02 כיול חיישן מים (°C)",-9.9,9.9,0.1],
 ["u30","U30 זמן מדחס עד הפשרה (דק')",10,90,1],
 ["u31","U31 טמפ' צנרת להתחלת הפשרה (°C)",-20,0,0.5],
 ["u32","U32 טמפ' צנרת לסיום הפשרה (°C)",2,30,0.5],
 ["u33","U33 זמן הפשרה מקסימלי (דק')",2,15,1],
 ["u34","U34 מצב שסתום",[[0,"0 — כבוי בחימום"],[1,"1 — פועל בחימום"]]],
 ["u50","U50 הגנת פליטה",[[0,"0 — כבויה"],[1,"1 — מאוורר ממשיך"],[2,"2 — מאוורר נעצר"]]],
 ["u51","U51 טמפ' הגנת פליטה (°C)",50,125,1],
 ["u52","U52 הגנת קפיאה",[[0,"כבוי"],[1,"פעיל"]]],
 ["u80","U80 הפעלה אוטומטית אחרי הפסקת חשמל",[[0,"כבוי"],[1,"פעיל"]]],
];
let last=null, busy=false;

function buildParams(){
  const g=$("params");
  for(const [k,label,a,b,step] of PARAMS){
    const l=document.createElement("label"); l.textContent=label; l.htmlFor="p_"+k;
    let el;
    if(Array.isArray(a)){
      el=document.createElement("select");
      for(const [v,t] of a){const o=document.createElement("option");o.value=v;o.textContent=t;el.appendChild(o);}
    }else{
      el=document.createElement("input"); el.type="number"; el.min=a; el.max=b; el.step=step;
      const s=document.createElement("small"); s.textContent=a+" – "+b; l.appendChild(s);
    }
    el.id="p_"+k; el.name=k; g.appendChild(l); g.appendChild(el);
  }
}
function fillForms(s){
  for(const [k] of PARAMS){ const el=$("p_"+k); el.value=(+s[k]).toString(); }
  const f=$("fNet"); f.mqttHost.value=s.mqttHost; f.mqttPort.value=s.mqttPort; f.mqttUser.value=s.mqttUser;
  f.mqttPass.value=""; f.webPass.value="";
}
const fmt=v=>v===undefined?"--":v.toFixed(1)+"°";
function setRelay(id,on){$(id).classList.toggle("on",!!on);}
function msg(id,text,cls){const m=$(id);m.textContent=text||"";m.className="msg "+(cls||"");}

async function api(path,body){
  const r=await fetch(path,body===undefined?{}:{method:"POST",headers:{"Content-Type":"application/json"},body:JSON.stringify(body)});
  const t=await r.text();
  if(!r.ok) throw new Error(t||r.status);
  return t;
}

async function poll(){
  try{
    const d=JSON.parse(await api("/api/status"));
    const first=!last; last=d;
    $("link").textContent=d.link?"מחובר לבקר":"אין קשר לבקר";
    $("link").className="pill "+(d.link?"ok":"bad");
    const e=$("err"); e.hidden=d.error==="none"; e.textContent=ERRORS[d.error]||d.error;
    $("state").textContent=STATES[d.state]||d.state;
    $("water").textContent=fmt(d.water); $("outdoor").textContent=fmt(d.outdoor);
    $("pipe").textContent=fmt(d.pipe); $("exhaust").textContent=fmt(d.exhaust);
    $("sp").textContent=Math.round(d.settings.sp)+"°";
    setRelay("rComp",d.comp); setRelay("rFan",d.fan); setRelay("rValve",d.valve); setRelay("rDef",d.defrost);
    $("btnPower").textContent=d.online?"כבה מערכת":"הפעל מערכת";
    $("btnPower").className=d.online?"":"pri";
    $("statusMsg").textContent="ריצת מדחס: "+d.compRunMin+" דק' · הפשרות: "+d.defrostCount;
    $("fwRemote").textContent=d.fw.remote+"  ("+d.fw.remoteBuilt+")";
    $("fwCtrl").textContent=d.fw.ctrl?d.fw.ctrl+"  ("+d.fw.ctrlBuilt+")":"גרסה ישנה (ללא תמיכה ב-OTA)";
    $("foot").textContent="WiFi "+d.rssi+" dBm · MQTT "+(d.mqtt?"מחובר":"לא מחובר")+" · פעיל "+Math.floor(d.uptime/3600)+" ש'";
    if(first) fillForms(d.settings);
    showOta(d.ota);
  }catch(err){
    $("link").textContent="אין קשר לשלט"; $("link").className="pill bad";
  }
}

function showOta(o){
  const p=$("prog");
  const active=["uploading","starting","sending","verifying"].includes(o.state);
  if(o.state==="idle"){ if(!busy) p.hidden=true; return; }
  if(busy && o.state==="uploading") return;   // XHR progress owns the bar
  p.hidden=!active; p.value=o.pct;
  if(active) msg("fwMsg","בקר: "+o.msg+" "+o.pct+"%","warn");
  else if(o.state==="done") msg("fwMsg","הבקר עודכן ומאתחל. הגרסה תתעדכן כאן תוך כמה שניות.","ok");
  else if(o.state==="failed") msg("fwMsg","עדכון הבקר נכשל: "+o.msg+". הבקר ממשיך עם הקושחה הקודמת.","bad");
  $("upCtrl").disabled=$("upRemote").disabled=active||busy;
}

function upload(path,input,label){
  const f=input.files[0];
  if(!f){msg("fwMsg","בחר קובץ .bin","bad");return;}
  if(!confirm("לעדכן את "+label+" עם "+f.name+"?")) return;
  const fd=new FormData(); fd.append("firmware",f,f.name);
  const x=new XMLHttpRequest(), p=$("prog");
  busy=true; p.hidden=false; p.value=0; $("upCtrl").disabled=$("upRemote").disabled=true;
  x.upload.onprogress=e=>{if(e.lengthComputable){p.value=Math.round(e.loaded*100/e.total);msg("fwMsg","מעלה "+label+"… "+p.value+"%","warn");}};
  x.onload=()=>{busy=false;
    if(x.status===200) msg("fwMsg",x.responseText==="OK - remote rebooting"?"השלט עודכן ומאתחל — הדף יחזור בעוד כ-15 שניות.":"הקובץ הועלה, שולח לבקר…","ok");
    else { msg("fwMsg","נכשל: "+x.responseText,"bad"); p.hidden=true; }
    $("upCtrl").disabled=$("upRemote").disabled=false; input.value="";
  };
  x.onerror=()=>{busy=false;msg("fwMsg","שגיאת תקשורת","bad");p.hidden=true;$("upCtrl").disabled=$("upRemote").disabled=false;};
  x.open("POST",path); x.send(fd);
}

$("btnPower").onclick=async()=>{ if(!last) return;
  const on=!last.online;
  if(!confirm(on?"להפעיל את המערכת?":"לכבות את המערכת?")) return;
  try{await api("/api/cmd",{power:on}); msg("statusMsg","הפקודה נשלחה","ok");}catch(e){msg("statusMsg",e.message,"bad");}
};
$("btnDefrost").onclick=async()=>{
  if(!confirm("להתחיל הפשרה יזומה? (פועל רק בזמן חימום)")) return;
  try{await api("/api/cmd",{defrost:true}); msg("statusMsg","הפקודה נשלחה","ok");}catch(e){msg("statusMsg",e.message,"bad");}
};
$("fSet").onsubmit=async e=>{ e.preventDefault();
  const body={}; for(const [k] of PARAMS) body[k]=parseFloat($("p_"+k).value);
  try{await api("/api/settings",body); msg("setMsg","נשמר ונשלח לבקר","ok"); last=null; poll();}catch(err){msg("setMsg",err.message,"bad");}
};
$("btnReload").onclick=()=>{ if(last) fillForms(last.settings); msg("setMsg",""); };
$("fNet").onsubmit=async e=>{ e.preventDefault();
  const f=e.target, body={mqttHost:f.mqttHost.value.trim(),mqttPort:parseInt(f.mqttPort.value),mqttUser:f.mqttUser.value};
  if(f.mqttPass.value) body.mqttPass=f.mqttPass.value;
  if(f.webPass.value) body.webPass=f.webPass.value;
  try{await api("/api/settings",body); msg("netMsg",body.webPass?"נשמר. הדפדפן יבקש את הסיסמה החדשה.":"נשמר","ok");}catch(err){msg("netMsg",err.message,"bad");}
};
$("upRemote").onclick=()=>upload("/update/remote",$("fileRemote"),"השלט");
$("upCtrl").onclick=()=>upload("/update/ctrl",$("fileCtrl"),"הבקר החיצוני");

buildParams(); poll(); setInterval(poll,2000);
</script>
</body>
</html>
)rawliteral";
