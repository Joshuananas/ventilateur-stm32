const SLOT_NAMES = {0:'⏻ Power',1:'Vitesse 1',2:'Vitesse 2',3:'Vitesse 3',4:'Vitesse 4',
                    5:'Vitesse 5',6:'Vitesse 6',7:'💡 Lumière',8:'↕ Reverse',
                    9:'−',10:'+',11:'Timer 1H',12:'Timer 3H',13:'Timer 6H'};
const learned = {};   // slot -> nb pulses (pour l'UI)
const saveData = {};  // slot -> [pulses] (pour sauvegarde)
let learnSlot = null; // slot en cours d'apprentissage
let mode = 'use'; // demarre en mode utilisation (les codes sont appris une fois)

// ---- Onglets ----
document.querySelectorAll('nav.tabs button').forEach(b=>{
  b.onclick = () => {
    document.querySelectorAll('nav.tabs button').forEach(x=>x.classList.remove('active'));
    document.querySelectorAll('.tabpane').forEach(x=>x.classList.remove('active'));
    b.classList.add('active');
    $('tab-'+b.dataset.tab).classList.add('active');
  };
});

// ---- Construction de la roue des vitesses (positions autour du ⏻) ----
const wheel = document.getElementById('wheel');
const pos = [[46,0],[88,22],[88,68],[46,90],[4,68],[4,22]];
const wheelSlots = [3,4,5,6,1,2];
wheelSlots.forEach((slot,i)=>{
  const b = document.createElement('button');
  b.dataset.slot = slot;
  b.textContent = slot;
  b.style.left = pos[i][0]+'px'; b.style.top = pos[i][1]+'px';
  wheel.appendChild(b);
});

let port, reader, keepReading = false;
const atBottom = b => (b.scrollHeight - b.scrollTop - b.clientHeight) < 40;
const log = m => { const b=document.getElementById('logbox');
  const stick = atBottom(b);
  b.textContent += m+"\n"; if (stick) b.scrollTop = b.scrollHeight; };
const _hhmmss = () => new Date().toLocaleTimeString('fr-FR');
const logEm = m => { const b=document.getElementById('embox');
  const stick = atBottom(b);
  b.innerHTML += `<span class="em">[${_hhmmss()}] ${m}</span>\n`; if (stick) b.scrollTop=b.scrollHeight; };
const logEv = (m,cls='rx') => { const b=document.getElementById('evbox');
  const stick = atBottom(b);
  b.innerHTML += `<span class="${cls}">[${_hhmmss()}] ${m}</span>\n`; if (stick) b.scrollTop=b.scrollHeight; };
const copyBox = async id => {
  const b = document.getElementById(id);
  try { await navigator.clipboard.writeText(b.innerText); }
  catch(e) {
    const r = document.createRange(); r.selectNodeContents(b);
    const s = getSelection(); s.removeAllRanges(); s.addRange(r);
  }
};
const $ = id => document.getElementById(id);
let observationExpected = 0;

async function connect() {
  try {
    port = await navigator.serial.requestPort();
    await port.open({ baudRate: 115200 });
    resetStateViews();
    $('connPill').textContent='connecté'; $('connPill').className='pill on';
    $('btnConn').textContent='Déconnecter';
    log('--- connecté @115200 ---');
    keepReading = true; readLoop();
    setTimeout(()=>send('status'), 500);
    setTimeout(()=>send('slots'), 1000); // recupere les boutons restores depuis la flash
    setTimeout(()=>send('savoir'), 1200); // + les scenarios figes appris
    setTimeout(()=>send('timesync '+Math.floor(Date.now()/1000)), 1500);
    setTimeout(()=>send('observations'), 2000);
    fetchMeteo(); // meteo immediate + toutes les 15 min
    meteoTimer = setInterval(fetchMeteo, 15*60*1000);
  } catch(e){ log('Erreur connexion: '+e); }
}

// ---------- Meteo Open-Meteo -> contexte STM32 ----------
const LAT = 43.83922958259251, LON = 2.0147621651163816;
let meteoTimer = null;
async function fetchMeteo() {
  try {
    const url = `https://api.open-meteo.com/v1/forecast?latitude=${LAT}&longitude=${LON}` +
      `&current=temperature_2m,relative_humidity_2m,shortwave_radiation,is_day&timezone=auto`;
    const r = await fetch(url);
    const j = await r.json();
    const c = j.current;
    log(`🌤 Météo : ${c.temperature_2m}°C ext, ${c.relative_humidity_2m}% HR, ` +
        `${Math.round(c.shortwave_radiation)} W/m²${c.is_day ? ' (jour)' : ' (nuit)'}`);
    logEv(`🌤 Météo : ${c.temperature_2m}°C / ${Math.round(c.shortwave_radiation)} W/m²`, 'beep');
    send(`meteo ${c.temperature_2m} ${c.relative_humidity_2m} ` +
         `${c.is_day ? Math.round(c.shortwave_radiation) : 0}`);
  } catch(e) { log('🌤 Météo indisponible : '+e); }
}
async function disconnect() {
  keepReading = false;
  resetStateViews();
  if (reader) { try{ await reader.cancel(); }catch(e){} }
  if (port) { try{ await port.close(); }catch(e){} }
  if (meteoTimer) { clearInterval(meteoTimer); meteoTimer=null; }
  $('connPill').textContent='déconnecté'; $('connPill').className='pill off';
  $('btnConn').textContent='Connecter';
}
$('btnConn').onclick = () => port && port.readable ? disconnect() : connect();

async function readLoop() {
  const dec = new TextDecoder(); let buf='';
  while (port.readable && keepReading) {
    reader = port.readable.getReader();
    try {
      while (true) {
        const {value,done} = await reader.read();
        if (done) break;
        buf += dec.decode(value);
        let i;
        while ((i = buf.indexOf('\n')) >= 0) { const l=buf.slice(0,i).replace(/\r$/,''); buf=buf.slice(i+1); handleLine(l); }
      }
    } catch(e){ log('Erreur lecture: '+e); break; }
    finally { reader.releaseLock(); }
  }
  // Le flux s'est termine (carte rebootee/debranchee) -> on informe l'UI
  if (port && !port.readable) {
    log('⚠ Connexion série perdue — la carte a peut-être redémarré.');
    await disconnect();
  }
}

