const $=id=>document.getElementById(id);
const phaseNames={idle:'Готов',precheck:'Проверка',warmup:'Прогрев',drying:'Сушка',paused:'Пауза',finish:'Завершение',cooldown:'Охлаждение',fault:'Авария'};
const faultNames={none:'',ntc_invalid:'Датчик NTC',heater_overtemperature:'Перегрев нагревателя',air_sensor_invalid:'Датчик воздуха',weight1_invalid:'Датчик веса 1',weight2_invalid:'Датчик веса 2',warmup_timeout:'Таймаут прогрева',configuration_invalid:'Конфигурация',watchdog_reset:'Watchdog'};
const modeNames={idle:'—',timed_preset:'Пресет',timed_manual:'Ручной',continuous:'Постоянный',cooldown:'Охлаждение',calibration:'Калибровка',fault:'Авария'};
let hist=[],live=[],uptimeMs=0,wallOff=null,tzOffMin=180,rangeSec=14400,hoverX=null,pinned=false,pinTs=null,geom=null,presets=[],calBusy=false;

async function request(url,options={}){const r=await fetch(url,options);if(!r.ok)throw new Error(r.status);return r.json()}

function fmtTime(sec){if(sec==null||sec<0)return '—';sec=Math.floor(sec);const h=Math.floor(sec/3600),m=Math.floor(sec%3600/60),s=sec%60;return (h?h+':':'')+String(m).padStart(2,'0')+':'+String(s).padStart(2,'0')}

function fmtAgo(sec){if(sec==null||sec<0)return '—';sec=Math.floor(sec);if(sec<5)return 'сейчас';if(sec<60)return sec+' с назад';if(sec<3600){const m=Math.floor(sec/60),s=sec%60;return m+' мин'+(s?' '+s+' с':'')+' назад'}const h=Math.floor(sec/3600),m=Math.round(sec%3600/60);return h+' ч'+(m?' '+m+' мин':'')+' назад'}

function pad2(n){return String(n).padStart(2,'0')}

function fmtTz(){const a=Math.abs(tzOffMin);return 'UTC'+(tzOffMin<0?'−':'+')+Math.floor(a/60)+(a%60?':'+pad2(a%60):'')}

// Wall-clock rendering in the device timezone (tzOffMin), not the browser's:
// labels match what the dryer itself shows.
function fmtWall(sec,withSec){
  const d=new Date((sec+tzOffMin*60)*1000),n=new Date((Date.now()/1000+tzOffMin*60)*1000);
  const hm=pad2(d.getUTCHours())+':'+pad2(d.getUTCMinutes())+(withSec?':'+pad2(d.getUTCSeconds()):'');
  const sameDay=d.getUTCFullYear()===n.getUTCFullYear()&&d.getUTCMonth()===n.getUTCMonth()&&d.getUTCDate()===n.getUTCDate();
  return sameDay?hm:pad2(d.getUTCDate())+'.'+pad2(d.getUTCMonth()+1)+' '+hm;
}

function render(s){
  $('connection').textContent=s.wifiConnected?'online':'offline';
  $('connection').className='badge '+(s.wifiConnected?'online':'');
  $('airTemp').textContent=Number(s.air.temperatureC).toFixed(1)+' °C';
  $('airRh').textContent='RH '+Number(s.air.relativeHumidity).toFixed(1)+' %';
  $('heaterTemp').textContent=Number(s.heater.temperatureC).toFixed(1)+' °C';
  $('heaterPower').textContent='нагрев '+Number(s.outputs.heater).toFixed(0)+' %';
  const hasSetpoints=s.setpoints&&s.setpoints.airTemperatureC>0;
  $('setTemp').textContent=hasSetpoints?Number(s.setpoints.airTemperatureC).toFixed(0)+' °C':'—';
  $('setRh').textContent=hasSetpoints&&s.setpoints.relativeHumidity>0?'RH '+Number(s.setpoints.relativeHumidity).toFixed(0)+' %':'—';
  const running=s.mode!=='idle'&&s.mode!=='cooldown'&&s.mode!=='fault';
  $('mode').textContent=running&&s.runLabel?s.runLabel:(modeNames[s.mode]||s.mode);
  const faultText=faultNames[s.fault]||'';
  $('phase').textContent=(phaseNames[s.phase]||s.phase)+(faultText?' · '+faultText:'');
  if(running&&s.mode==='continuous'){$('timeLeft').textContent=fmtTime(s.elapsedSeconds);$('timeNote').textContent='прошло';}
  else if(running){$('timeLeft').textContent=fmtTime(s.remainingSeconds);$('timeNote').textContent='осталось';}
  else{$('timeLeft').textContent='—';$('timeNote').textContent='';}
  $('fanPower').textContent=Number(s.outputs.fan).toFixed(0)+' %';
  $('ventAngle').textContent='заслонка '+Number(s.outputs.ventAngle).toFixed(0)+'°';
  $('weight1').textContent=Number(s.weights.one).toFixed(0);
  $('weight2').textContent=Number(s.weights.two).toFixed(0);
  $('weightTotal').textContent=Number(s.weights.total).toFixed(0);
  $('pause').textContent=s.phase==='paused'?'Продолжить':'Пауза';
  $('netStatus').textContent=s.apActive
    ?'Режим настройки: точка доступа FilamentDryer-Setup, веб-панель по адресу '+(s.ip||'192.168.4.1')+'. Задайте домашнюю сеть ниже.'
    :(s.wifiConnected?'Wi-Fi подключён · адрес: '+((s.hostname||'dryer')+'.local')+(s.ip?' ('+s.ip+')':''):'Wi-Fi не подключён');
  if(s.uptimeMs!=null){if(uptimeMs&&s.uptimeMs<uptimeMs-5000){live=[];hist=[];wallOff=null;loadHistory()}uptimeMs=s.uptimeMs}
  if(s.tzOffsetMinutes!=null)tzOffMin=s.tzOffsetMinutes;
  if(s.timeSynced&&s.epochSeconds&&wallOff==null){wallOff=s.epochSeconds-uptimeMs/1000;live.forEach(p=>{p.wall=p.ts+wallOff})}
  const ts=uptimeMs/1000;
  const t=Number.isFinite(+s.air.temperatureC)?+s.air.temperatureC:null;
  const r=Number.isFinite(+s.air.relativeHumidity)?+s.air.relativeHumidity:null;
  const n=s.heater&&s.heater.valid!==false&&Number.isFinite(+s.heater.temperatureC)?+s.heater.temperatureC:null;
  if(t!=null||r!=null||n!=null){
    const last=live[live.length-1];
    if(!last||ts-last.ts>=5){live.push({ts,t,r,n,wall:wallOff==null?null:ts+wallOff});if(live.length>3200)live.shift()}
  }
  $('lgTemp').textContent=t!=null?t.toFixed(1)+' °C':'—';
  $('lgRh').textContent=r!=null?r.toFixed(1)+' %':'—';
  $('lgNtc').textContent=n!=null?n.toFixed(1)+' °C':'—';
  $('syncStatus').textContent=wallOff!=null
    ?'Время синхронизировано (NTP) · '+fmtTz()+'.'
    :(s.ntpEnabled===false
      ?'Синхронизация времени отключена — метки времени относительные.'
      :'Часы не синхронизированы — метки времени относительные ('+fmtTz()+').');
  draw();
}

const C_T='#55d6be',C_H='#ffbd69',C_N='#ff6b7a';

function niceTicks(min,max,count){
  const span=(max-min)||1,raw=span/count,mag=Math.pow(10,Math.floor(Math.log10(raw))),norm=raw/mag;
  const step=(norm<1.5?1:norm<3.5?2:norm<7.5?5:10)*mag;
  const ticks=[];for(let v=Math.ceil(min/step)*step;v<=max+1e-9;v+=step)ticks.push(v);
  return ticks;
}

function timeStep(sec){const steps=[30,60,120,300,600,900,1800,3600,7200,10800,21600,43200];for(const s of steps)if(sec/s<=8)return s;return 86400}

function fmtAxis(sec){if(sec<=0)return 'сейчас';if(sec<60)return '−'+Math.round(sec)+' с';if(sec<3600)return '−'+Math.round(sec/60)+' мин';const h=Math.floor(sec/3600),m=Math.round(sec%3600/60);return m?'−'+h+' ч '+m+' мин':'−'+h+' ч'}

function fmtWallAxis(sec,step){const d=new Date((sec+tzOffMin*60)*1000);if(step>=86400)return pad2(d.getUTCDate())+'.'+pad2(d.getUTCMonth()+1);const hm=pad2(d.getUTCHours())+':'+pad2(d.getUTCMinutes());return step>=3600?pad2(d.getUTCDate())+'.'+pad2(d.getUTCMonth()+1)+' '+hm:hm}

function series(){const lastHist=hist.length?hist[hist.length-1].ts:-1;return hist.concat(live.filter(p=>p.ts>lastHist))}

function draw(){
  const c=$('chart'),x=c.getContext('2d'),w=c.clientWidth||700,h=220,d=devicePixelRatio||1;
  c.width=w*d;c.height=h*d;x.setTransform(d,0,0,d,0,0);x.clearRect(0,0,w,h);
  const ML=46,MR=46,MT=10,MB=24,pw=w-ML-MR,ph=h-MT-MB;
  const data=series();
  // Wall-clock mode: every visible sample is anchored to real time via NTP.
  const wallMode=wallOff!=null&&data.length>0&&data.every(p=>p.wall!=null);
  const keyOf=p=>wallMode?p.wall:p.ts;
  const nowKey=wallMode?uptimeMs/1000+wallOff:Math.max(uptimeMs/1000,data.length?keyOf(data[data.length-1]):0);
  const k0=nowKey-rangeSec;
  const view=data.filter(p=>keyOf(p)>=k0-60);
  x.font='11px system-ui';
  let tMin=Infinity,tMax=-Infinity;
  view.forEach(p=>{[p.t,p.n].forEach(v=>{if(v!=null){if(v<tMin)tMin=v;if(v>tMax)tMax=v}})});
  if(!view.length){tMin=0;tMax=40}
  if(tMax-tMin<5){const m=(tMax+tMin)/2;tMin=m-2.5;tMax=m+2.5}
  tMin=Math.floor(tMin);tMax=Math.ceil(tMax);
  const yT=v=>MT+ph-(v-tMin)/(tMax-tMin)*ph;
  const yH=v=>MT+ph-v/100*ph;
  const pxOf=key=>ML+(key-k0)/rangeSec*pw;
  geom={ML,MR,pw,k0,nowKey};
  x.textBaseline='middle';x.strokeStyle='#1d2836';x.lineWidth=1;
  x.fillStyle=C_T;x.textAlign='right';
  niceTicks(tMin,tMax,5).forEach(v=>{const y=yT(v);if(y<MT-1||y>MT+ph+1)return;x.beginPath();x.moveTo(ML,y);x.lineTo(w-MR,y);x.stroke();x.fillText(v.toFixed(0),ML-7,y)});
  x.fillStyle=C_H;x.textAlign='left';
  [0,25,50,75,100].forEach(v=>{const y=yH(v);if(y<MT-1||y>MT+ph+1)return;x.fillText(String(v),w-MR+7,y)});
  x.fillStyle='#8fa2b5';x.textAlign='center';x.textBaseline='top';
  const step=timeStep(rangeSec);
  if(wallMode){
    for(let tk=Math.floor(k0/step)*step;tk<=nowKey;tk+=step){
      const px=pxOf(tk);
      x.strokeStyle='#1d2836';x.beginPath();x.moveTo(px,MT);x.lineTo(px,MT+ph);x.stroke();
      if(px<=w-MR-34)x.fillText(fmtWallAxis(tk,step),px,MT+ph+7);
    }
  }else{
    for(let k=Math.floor(rangeSec/step);k>=1;k--){
      const px=pxOf(nowKey-k*step);
      x.strokeStyle='#1d2836';x.beginPath();x.moveTo(px,MT);x.lineTo(px,MT+ph);x.stroke();
      x.fillText(fmtAxis(k*step),px,MT+ph+7);
    }
  }
  x.fillText('сейчас',w-MR,MT+ph+7);
  if(view.length>1){
    [['t',C_T,v=>yT(v)],['n',C_N,v=>yT(v)],['r',C_H,v=>yH(v)]].forEach(([key,color,yv])=>{
      x.strokeStyle=color;x.lineWidth=1.7;x.lineJoin='round';x.beginPath();
      let started=false;
      view.forEach(p=>{const v=p[key];if(v==null){started=false;return}
        const px=pxOf(keyOf(p)),py=yv(v);
        if(started)x.lineTo(px,py);else{x.moveTo(px,py);started=true}});
      x.stroke();
    });
    x.lineWidth=1;
  }
  let keyTarget=null;
  if(pinned)keyTarget=pinTs;
  else if(hoverX!=null&&hoverX>=ML&&hoverX<=w-MR)keyTarget=k0+(hoverX-ML)/pw*rangeSec;
  drawHover(x,w,h,ML,MR,MT,MB,pw,ph,nowKey,yT,yH,view,keyTarget,keyOf,pxOf,wallMode);
}

function drawHover(x,w,h,ML,MR,MT,MB,pw,ph,nowKey,yT,yH,view,keyTarget,keyOf,pxOf,wallMode){
  const tip=$('chartTip');
  if(keyTarget==null||keyTarget<nowKey-rangeSec-60||!view.length){tip.hidden=true;return}
  let best=null,bd=Infinity;
  view.forEach(p=>{const dd=Math.abs(keyOf(p)-keyTarget);if(dd<bd){bd=dd;best=p}});
  if(!best||bd>120){tip.hidden=true;return}
  const cx=pxOf(keyOf(best));
  x.strokeStyle='#4a5d77';x.setLineDash([4,4]);x.beginPath();x.moveTo(cx,MT);x.lineTo(cx,MT+ph);x.stroke();x.setLineDash([]);
  const marks=[];
  if(best.t!=null)marks.push([yT(best.t),C_T]);
  if(best.n!=null)marks.push([yT(best.n),C_N]);
  if(best.r!=null)marks.push([yH(best.r),C_H]);
  marks.forEach(pair=>{x.fillStyle=pair[1];x.beginPath();x.arc(cx,pair[0],3.5,0,7);x.fill()});
  const anchorY=best.t!=null?yT(best.t):(best.n!=null?yT(best.n):MT+ph/2);
  const ago=fmtAgo(nowKey-keyOf(best));
  const when=(wallMode&&best.wall!=null)?fmtWall(best.wall,true)+' ('+fmtTz()+') · '+ago:ago;
  tip.hidden=false;
  tip.innerHTML='<div class="tt-time">'+when+'</div>'
    +'<div class="row"><span><i style="background:'+C_T+'"></i>Температура</span><b>'+(best.t!=null?best.t.toFixed(1)+' °C':'—')+'</b></div>'
    +'<div class="row"><span><i style="background:'+C_N+'"></i>Нагреватель</span><b>'+(best.n!=null?best.n.toFixed(1)+' °C':'—')+'</b></div>'
    +'<div class="row"><span><i style="background:'+C_H+'"></i>Влажность</span><b>'+(best.r!=null?best.r.toFixed(1)+' %':'—')+'</b></div>'
    +(pinned?'<div class="tt-pin">закреплено · клик по графику, чтобы скрыть</div>':'');
  const tw=tip.offsetWidth,th=tip.offsetHeight;
  let lx=cx+13;if(lx+tw>w-4)lx=cx-tw-13;let ly=Math.max(4,Math.min(h-th-4,anchorY-th/2));
  tip.style.left=lx+'px';tip.style.top=ly+'px';
}

async function loadHistory(){
  let text='';
  try{const r=await fetch('/api/history');if(r.ok)text=await r.text()}catch(e){}
  const runs=[[]];let prev=-1;
  const num=v=>v==null?null:(Number.isFinite(+v)?+v:null);
  text.split('\n').forEach(l=>{const s=l.trim();if(!s)return;let p;try{p=JSON.parse(s)}catch(e){return}
    if(!p||p.ts==null)return;
    if(prev>=0&&p.ts<prev-60000)runs.push([]);
    runs[runs.length-1].push({ts:p.ts/1000,t:num(p.t),r:num(p.rh),n:num(p.ntc),wall:p.epoch?+p.epoch:null});
    prev=p.ts});
  // Anchor each uptime-based run to the wall clock through its last record
  // stamped by NTP, so old samples also display real timestamps.
  hist=[].concat(...runs.map(run=>{
    let anchor=null;run.forEach(p=>{if(p.wall!=null)anchor=p});
    if(anchor){const off=anchor.wall-anchor.ts;run.forEach(p=>{p.wall=p.ts+off})}
    return run;}));
  draw();
}

const eventTypeNames={run:'Запуск',stop:'Останов',pause:'Пауза',resume:'Продолжение',calibration:'Калибровка',config:'Конфигурация',ota:'OTA-обновление',reboot:'Перезагрузка',ntp:'Время'};

function fmtEvents(text){
  const out=[];
  text.split('\n').forEach(l=>{const s=l.trim();if(!s)return;let ev;try{ev=JSON.parse(s)}catch(e){out.push(s);return}
    if(!ev||ev.ts==null)return;
    let when='—';
    if(ev.epoch)when=fmtWall(ev.epoch,false);
    else if(uptimeMs&&ev.ts<=uptimeMs)when=fmtAgo(uptimeMs/1000-ev.ts/1000);
    out.push(when+' · '+(eventTypeNames[ev.type]||ev.type)+(ev.message?' — '+ev.message:''));
  });
  out.reverse();
  return out.length?out.join('\n'):'Нет событий';
}

let evTick=0;
async function refresh(){
  try{render(await request('/api/state'))}catch(e){$('connection').textContent='нет связи';$('connection').className='badge'}
  try{if(evTick++%10===0){const e=await fetch('/api/events');$('events').textContent=e.ok?fmtEvents(await e.text()):'Нет событий'}}catch(e){}
  if(document.getElementById('tab-calibration').classList.contains('active'))refreshCal();
}

async function refreshCal(){
  let c;try{c=await request('/api/calibration')}catch(e){return}
  const drift=c.active
    ?(c.phase==='heat'
      ?'Прогрев: '+Number(c.airTempC).toFixed(1)+' °C → цель '+Number(c.targetTempC).toFixed(0)+' °C · точек: '+c.points
      :'Охлаждение: '+Number(c.airTempC).toFixed(1)+' °C → до '+Number(c.startTempC).toFixed(0)+' °C · точек: '+c.points)
    :'Температурная калибровка не активна.';
  $('calDriftStatus').textContent=drift;
  $('driftStart').style.display=c.active?'none':'';
  $('driftCancel').style.display=c.active?'':'none';
  for(let i=0;i<2;i++){
    const sp=c.spools&&c.spools[i];if(!sp)continue;
    $('calRaw'+(i+1)).textContent=sp.present?Number(sp.raw).toFixed(0):'нет';
    $('calInfo'+(i+1)).textContent=(sp.present?Number(sp.grams).toFixed(0)+' г · делитель '+Number(sp.scale).toFixed(1):'датчик не отвечает')
      +(sp.calValid?' · термокомпенсация: '+sp.calBands+' диапазонов':' · без термокомпенсации');
  }
}

function calAction(body,msg){calBusy=true;request('/api/calibration',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(()=>{if(msg)alert(msg);refreshCal()}).catch(e=>alert('Ошибка: '+e.message)).finally(()=>{calBusy=false})}

$('tare').onclick=()=>calAction({action:'tare'},'Весы обнулены');
$('cal1').onclick=()=>calAction({action:'scale',spool:0,knownGrams:+$('known1').value});
$('cal2').onclick=()=>calAction({action:'scale',spool:1,knownGrams:+$('known2').value});
$('driftStart').onclick=()=>{
  if(!confirm('Запустить температурную калибровку? Груз известной массы должен оставаться на весах всё время. Камера нагреется примерно до 73 °C и будет остывать — это займёт несколько часов. Продолжить?'))return;
  calAction({action:'drift_start'});
};
$('driftCancel').onclick=()=>{if(confirm('Отменить температурную калибровку? Данные этого прогона будут потеряны.'))calAction({action:'drift_cancel'})};

document.querySelectorAll('.tab').forEach(t=>t.onclick=()=>{
  document.querySelectorAll('.tab').forEach(x=>x.classList.remove('active'));
  document.querySelectorAll('.tabbody').forEach(x=>x.classList.remove('active'));
  t.classList.add('active');
  $('tab-'+t.dataset.tab).classList.add('active');
  if(t.dataset.tab==='calibration')refreshCal();
});

function applyPreset(){if(!presets.length)return;const p=presets.find(q=>q.id===$('preset').value)||presets[0];$('targetTemp').value=p.temperatureC;$('targetRh').value=p.relativeHumidity;$('duration').value=Math.round(p.durationSeconds/1800)/2;}

function syncForm(){const mode=$('runMode').value,isPreset=mode==='timed_preset',isCont=mode==='continuous';
$('presetRow').style.display=isPreset?'':'none';
$('durationRow').style.display=isCont?'none':'';
$('targetTemp').disabled=isPreset;$('targetRh').disabled=isPreset;$('duration').disabled=isPreset;
if(isPreset)applyPreset();}

function fillScan(r){
  const sel=$('wifiSsid');sel.innerHTML='';
  const list=(r.networks||[]).sort((a,b)=>b.rssi-a.rssi);
  const manual=document.createElement('option');manual.value='';manual.textContent='— ввести вручную —';sel.appendChild(manual);
  list.forEach(n=>{const o=document.createElement('option');o.value=n.ssid;o.textContent=(n.ssid||'(скрытая сеть)')+' · '+n.rssi+' dBm'+(n.secure?' · защищённая':'');sel.appendChild(o)});
  if(!list.length){const none=document.createElement('option');none.value='';none.textContent='Сети не найдены';sel.appendChild(none)}
}

async function pollScan(){try{const r=await request('/api/scan');if(r.scanning){setTimeout(pollScan,1500)}else{fillScan(r)}}catch(e){}}

$('scan').onclick=()=>{const sel=$('wifiSsid');sel.innerHTML='<option>Сканирование…</option>';request('/api/scan').then(r=>{if(r.scanning)setTimeout(pollScan,1500)}).catch(()=>{fillScan({networks:[]})})};

$('saveWifi').onclick=()=>{
  const ssid=$('wifiSsidManual').value.trim()||$('wifiSsid').value;
  const password=$('wifiPassword').value;
  if(!ssid){alert('Укажите имя сети (SSID)');return}
  request('/api/config',{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify({wifiSsid:ssid,wifiPassword:password})})
    .then(()=>request('/api/reboot',{method:'POST'}))
    .then(()=>{alert('Настройки сохранены. Устройство перезагружается и подключится к сети «'+ssid+'». После этого веб-панель будет доступна по новому адресу в домашней сети.')})
    .catch(e=>alert('Ошибка сохранения: '+e.message));
};

$('saveTime').onclick=()=>{
  const body={ntpEnabled:$('ntpEnabled').checked,tzOffsetMinutes:+$('timezone').value,ntpServer:$('ntpServer').value.trim()};
  request('/api/config',{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})
    .then(()=>{alert('Настройки времени сохранены. Синхронизация выполнится автоматически, когда устройство подключено к сети с доступом в интернет.')})
    .catch(e=>alert('Ошибка сохранения: '+e.message));
};

function fillTimezones(){
  const sel=$('timezone');sel.innerHTML='';
  for(let m=-720;m<=840;m+=30){
    const a=Math.abs(m),h=Math.floor(a/60),mm=a%60;
    const o=document.createElement('option');o.value=m;
    o.textContent='UTC'+(m<0?'−':'+')+h+(mm?':'+pad2(mm):'');
    sel.appendChild(o);
  }
}

async function loadConfig(){try{const cfg=await request('/api/config');presets=cfg.presets||[];const sel=$('preset');sel.innerHTML='';presets.forEach(p=>{const o=document.createElement('option');o.value=p.id;o.textContent=p.name+' · '+p.temperatureC+'°C · '+Math.round(p.durationSeconds/3600)+' ч';sel.appendChild(o)});applyPreset();syncForm();$('wifiSsidManual').value=cfg.wifiSsid||'';$('ntpEnabled').checked=cfg.ntpEnabled!==false;fillTimezones();$('timezone').value=String(cfg.tzOffsetMinutes!=null?cfg.tzOffsetMinutes:180);$('ntpServer').value=cfg.ntpServer||'';$('webLogin').value=cfg.webLogin||'';$('secStatus').textContent=cfg.hasWebPassword?'Вход включён · логин: «'+(cfg.webLogin||'admin')+'»':'Вход отключён — панель открыта в локальной сети';}catch(e){}}

$('saveSecurity').onclick=()=>{
  const pw=$('webPassword').value;
  request('/api/config',{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify({webLogin:$('webLogin').value.trim(),webPassword:pw})})
    .then(()=>{
      if(pw){alert('Вход включён. Страница перезагрузится — введите новый логин и пароль.');location.reload();}
      else{alert('Вход отключён: панель открыта.');$('webPassword').value='';loadConfig();}
    })
    .catch(e=>alert('Ошибка: '+e.message));
};

$('runMode').onchange=syncForm;
$('preset').onchange=()=>{if($('runMode').value==='timed_preset')applyPreset()};
$('start').onclick=()=>{const mode=$('runMode').value,body={mode};
if(mode==='timed_preset')body.preset=$('preset').value;
else{body.temperatureC=+$('targetTemp').value;body.relativeHumidity=+$('targetRh').value;if(mode!=='continuous')body.durationSeconds=Math.round(+$('duration').value*3600);}
request('/api/run',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}).then(refresh).catch(()=>{})};
$('pause').onclick=()=>request('/api/pause',{method:'POST'}).then(refresh).catch(()=>{});
$('stop').onclick=()=>request('/api/stop',{method:'POST'}).then(refresh).catch(()=>{});

const chart=$('chart');
chart.addEventListener('pointermove',e=>{hoverX=e.offsetX;if(!pinned)draw()});
chart.addEventListener('pointerleave',()=>{if(!pinned){hoverX=null;draw()}});
chart.addEventListener('pointerdown',e=>{
  if(!geom)return;
  const key=geom.k0+(e.offsetX-geom.ML)/geom.pw*rangeSec;
  if(pinned){pinned=false;pinTs=null;hoverX=e.offsetX}
  else{pinned=true;pinTs=key;hoverX=e.offsetX}
  draw();
});
document.querySelectorAll('#rangebar .rg').forEach(b=>b.onclick=()=>{
  rangeSec=+b.dataset.range;
  document.querySelectorAll('#rangebar .rg').forEach(q=>q.classList.toggle('active',q===b));
  draw();
});
$('histReload').onclick=()=>loadHistory();
window.addEventListener('resize',draw);

setInterval(refresh,1000);refresh();loadConfig();loadHistory();
setInterval(loadHistory,300000);
