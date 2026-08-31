const $=id=>document.getElementById(id);
const phaseNames={idle:'Готов',precheck:'Проверка',warmup:'Прогрев',drying:'Сушка',paused:'Пауза',finish:'Завершение',cooldown:'Охлаждение',fault:'Авария'};
const faultNames={none:'',ntc_invalid:'Датчик NTC',heater_overtemperature:'Перегрев нагревателя',air_sensor_invalid:'Датчик воздуха',weight1_invalid:'Датчик веса 1',weight2_invalid:'Датчик веса 2',warmup_timeout:'Таймаут прогрева',configuration_invalid:'Конфигурация',watchdog_reset:'Watchdog'};
const modeNames={idle:'—',timed_preset:'Пресет',timed_manual:'Ручной',continuous:'Постоянный',cooldown:'Охлаждение',calibration:'Калибровка',fault:'Авария'};
let points=[],presets=[],calBusy=false;

async function request(url,options={}){const r=await fetch(url,options);if(!r.ok)throw new Error(r.status);return r.json()}

function fmtTime(sec){if(sec==null||sec<0)return '—';sec=Math.floor(sec);const h=Math.floor(sec/3600),m=Math.floor(sec%3600/60),s=sec%60;return (h?h+':':'')+String(m).padStart(2,'0')+':'+String(s).padStart(2,'0')}

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
  points.push({t:s.air.temperatureC,r:s.air.relativeHumidity});
  if(points.length>120)points.shift();
  draw();
}

function draw(){const c=$('chart'),x=c.getContext('2d'),w=c.clientWidth||700,h=160,d=devicePixelRatio||1;c.width=w*d;c.height=h*d;x.scale(d,d);x.clearRect(0,0,w,h);x.fillStyle='#8fa2b5';x.font='11px system-ui';x.fillText('t,°C',6,12);x.fillText('RH,%',48,12);if(points.length<2)return;
let tMin=Math.min(...points.map(p=>p.t)),tMax=Math.max(...points.map(p=>p.t));
if(tMax-tMin<5){const mid=(tMax+tMin)/2;tMin=mid-2.5;tMax=mid+2.5;}
const px=i=>i*w/(points.length-1);
x.strokeStyle='#55d6be';x.beginPath();points.forEach((p,i)=>{const py=h-(p.t-tMin)/(tMax-tMin)*h;i?x.lineTo(px(i),py):x.moveTo(px(i),py)});x.stroke();
x.strokeStyle='#ffbd69';x.beginPath();points.forEach((p,i)=>{const py=h-p.r/100*h;i?x.lineTo(px(i),py):x.moveTo(px(i),py)});x.stroke()}

async function refresh(){try{render(await request('/api/state'));const e=await fetch('/api/events');$('events').textContent=e.ok?await e.text():'Нет событий'}catch(e){$('connection').textContent='нет связи';$('connection').className='badge'}
if(document.getElementById('tab-calibration').classList.contains('active'))refreshCal();}

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

async function loadConfig(){try{const cfg=await request('/api/config');presets=cfg.presets||[];const sel=$('preset');sel.innerHTML='';presets.forEach(p=>{const o=document.createElement('option');o.value=p.id;o.textContent=p.name+' · '+p.temperatureC+'°C · '+Math.round(p.durationSeconds/3600)+' ч';sel.appendChild(o)});applyPreset();syncForm();$('wifiSsidManual').value=cfg.wifiSsid||'';$('webLogin').value=cfg.webLogin||'';$('secStatus').textContent=cfg.hasWebPassword?'Вход включён · логин: «'+(cfg.webLogin||'admin')+'»':'Вход отключён — панель открыта в локальной сети';}catch(e){}}

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
setInterval(refresh,1000);refresh();loadConfig();
