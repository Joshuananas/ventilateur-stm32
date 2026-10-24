$('btnSync').onclick = () => send(`sync ${$('syP').value} ${$('syS').value} ${$('syR').value} ${$('syL').value}`);

function markLearned(slot) {
  const b = document.querySelector(`button[data-slot="${slot}"]`);
  if (b) { b.classList.remove('learning','miss'); b.classList.add('learned'); }
  updateWizList();
}

function finishLearn(n) {
  if (learnSlot === null) return;
  const b = document.querySelector(`button[data-slot="${learnSlot}"]`);
  if (n > 8) {
    learned[learnSlot] = n;
    if (b) { b.classList.remove('learning'); b.classList.add('learned'); }
    $('wizMsg').innerHTML = `✅ <b>${SLOT_NAMES[learnSlot]}</b> appris ! (${n} pulses)<br>Clique un autre bouton pour continuer.`;
    $('wizMsg').className = 'step done';
  } else {
    if (b) { b.classList.remove('learning'); b.classList.add('miss'); }
    $('wizMsg').innerHTML = `❌ Rien capté pour <b>${SLOT_NAMES[learnSlot]}</b>. Réessaie : clique le bouton puis appuie 1-2 s sur la vraie télécommande, bien proche du module.`;
  }
  learnSlot = null;
  updateWizList();
}

function updateWizList() {
  const total = Object.keys(SLOT_NAMES).length;
  const n = Object.keys(SLOT_NAMES).filter(s=>learned[s]).length;
  $('wizList').innerHTML = `📻 <b>${n}/${total}</b> boutons appris — la cochette ✓ verte est directement sur la télécommande.` +
    (n < total ? ' <span style="color:var(--warn)">Grisés = pas encore appris.</span>' : '');
  document.querySelectorAll('.remote button').forEach(b=>{
    b.classList.toggle('dimmed', !learned[b.dataset.slot]);
  });
}

// ---- clic sur les boutons de la télécommande virtuelle ----
// − / + : en mode utilisation, émission répétée tant que le bouton est maintenu
let holdTimer = null;
function startHold(slot) {
  if (mode !== 'use' || !learned[slot] || holdTimer) return;
  $('wizMsg').innerHTML = `📡 <b>${SLOT_NAMES[slot]}</b> luminosité… (maintiens)`;
  send('play'+slot);
  holdTimer = setInterval(()=>send('play'+slot), 400);
}
function stopHold() { if (holdTimer) { clearInterval(holdTimer); holdTimer = null;
  if (mode==='use') $('wizMsg').innerHTML='Mode <b>utilisation</b> : clique un bouton pour émettre, maintiens −/+ pour régler la luminosité.'; } }
document.querySelectorAll('button[data-slot="9"],button[data-slot="10"]').forEach(b=>{
  const slot = +b.dataset.slot;
  b.addEventListener('mousedown', ()=>startHold(slot));
  b.addEventListener('touchstart', e=>{ e.preventDefault(); startHold(slot); }, {passive:false});
});
['mouseup','touchend','touchcancel','pointerup','pointercancel','blur'].forEach(ev=>
  window.addEventListener(ev, stopHold));

document.querySelectorAll('.remote button').forEach(b=>{
  b.onclick = () => {
    const slot = +b.dataset.slot;
    if (mode === 'use' && (slot === 9 || slot === 10)) return; // gere par mousedown/hold
    if (mode === 'learn') {
      if (learnSlot !== null) { $('wizMsg').textContent = 'Une capture est déjà en cours…'; return; }
      learnSlot = slot;
      document.querySelectorAll('.remote button').forEach(x=>x.classList.remove('miss'));
      b.classList.remove('learned'); b.classList.add('learning');
      $('wizMsg').className='step';
      $('wizMsg').innerHTML = `🎯 Apprentissage <b>${SLOT_NAMES[slot]}</b> :<br>appuie 1-2 s sur le bouton <b>${SLOT_NAMES[slot]}</b> de la vraie télécommande…`;
      send('learn'+slot);
    } else {
      if (!learned[slot]) { $('wizMsg').innerHTML = `⚠ <b>${SLOT_NAMES[slot]}</b> pas encore appris — passe en mode apprentissage.`; return; }
      $('wizMsg').innerHTML = `📡 Émission <b>${SLOT_NAMES[slot]}</b>…`;
      send('play'+slot);
    }
  };
});

function clearLearningState() {
  learnSlot = null;
  document.querySelectorAll('.remote button').forEach(x=>x.classList.remove('learning'));
}
$('mLearn').onclick = () => { clearLearningState(); mode='learn'; $('mLearn').classList.add('active'); $('mUse').classList.remove('active');
  $('wizMsg').innerHTML='Mode <b>apprentissage</b> : clique un bouton puis appuie sur la vraie télécommande.'; };
$('mUse').onclick = () => { clearLearningState(); mode='use'; $('mUse').classList.add('active'); $('mLearn').classList.remove('active');
  $('wizMsg').innerHTML='Mode <b>utilisation</b> : clique un bouton appris (✅) pour émettre son signal.'; };

// ---- Sauvegarde / chargement ----
$('btnSave').onclick = async () => {
  const slots = Object.keys(learned);
  if (!slots.length) { $('saveInfo').textContent = 'Rien à sauvegarder'; return; }
  for (const s of slots) {
    send('dump'+s);
    await new Promise(r=>setTimeout(r, 2500)); // laisse le dump arriver
  }
  localStorage.setItem('fanRemote', JSON.stringify(saveData));
  const blob = new Blob([JSON.stringify(saveData)], {type:'application/json'});
  const a = document.createElement('a');
  a.href = URL.createObjectURL(blob); a.download = 'telecommande.json'; a.click();
  $('saveInfo').textContent = `Sauvegardé (${slots.length} boutons) + fichier telecommande.json`;
};

$('btnLoad').onclick = async () => {
  let data = localStorage.getItem('fanRemote');
  if (!data) {
    const inp = document.createElement('input');
    inp.type = 'file'; inp.accept = '.json';
    inp.onchange = async () => {
      data = await inp.files[0].text();
      doLoad(JSON.parse(data));
    };
    inp.click();
    return;
  }
  doLoad(JSON.parse(data));
};

async function doLoad(data) {
  for (const [s, pulses] of Object.entries(data)) {
    if (!pulses || !pulses.length) continue;
    send('load'+s+' '+pulses.join(','));
    learned[s] = pulses.length;
    saveData[s] = pulses;
    markLearned(+s);
    await new Promise(r=>setTimeout(r, 300));
  }
  $('saveInfo').textContent = `Chargé (${Object.keys(data).length} boutons)`;
}

async function send(cmd) {
  if (!port || !port.writable) {
    log('Non connecté');
    const w = $('wizMsg');
    if (w) w.innerHTML = '⚠ <b>Pas connecté</b> — clique « Connecter » en haut d\u2019abord.';
    return;
  }
  // Seules les commandes RF arment la fenetre de correlation du bip
  if (cmd.startsWith('play') || cmd.startsWith('forge')) lastCmdTime = performance.now();
  logEm(cmd);
  const w = port.writable.getWriter();
  await w.write(new TextEncoder().encode(cmd+'\n'));
  w.releaseLock();
}
document.querySelectorAll('button[data-cmd]').forEach(b=>b.onclick=()=>send(b.dataset.cmd));
$('btnSend').onclick=()=>{const v=$('cmdIn').value.trim(); if(v){send(v);$('cmdIn').value='';}};
$('cmdIn').addEventListener('keydown',e=>{if(e.key==='Enter')$('btnSend').click();});
$('btnClear').onclick=()=>$('logbox').textContent='';
$('btnBottom').onclick=()=>{const b=$('logbox');b.scrollTop=b.scrollHeight;};
document.querySelectorAll('[data-copy]').forEach(btn=>{
  btn.onclick=()=>{ copyBox(btn.dataset.copy); btn.textContent='✓ copié';
    setTimeout(()=>btn.textContent='📋 Copier',1500); };
});

// ---------- Détection du bip du ventilateur (micro du PC) ----------
let micStream=null, audioCtx=null, micAnalyser=null, micBuf=null;
let micNoise=0, micBeepUntil=0, micLastBeep=0;
let lastBeepTime=0; // timestamp du dernier bip detecte (pour la verification)
let lastCmdTime=0;  // timestamp du dernier ordre envoye (correlation bip<->commande)
let lastHeardTime=0; // timestamp de la derniere activite telecommande physique
async function toggleMic() {
  const btn=$('btnMic');
  if (micStream) {
    micStream.getTracks().forEach(t=>t.stop());
    micStream=null; if(audioCtx) audioCtx.close();
    btn.textContent='🎤 Bip off';
    log('🎤 Détection de bip arrêtée');
    return;
  }
  try {
    micStream = await navigator.mediaDevices.getUserMedia({audio:true});
    audioCtx = new (window.AudioContext||window.webkitAudioContext)();
    const src = audioCtx.createMediaStreamSource(micStream);
    micAnalyser = audioCtx.createAnalyser();
    micAnalyser.fftSize = 2048;
    src.connect(micAnalyser);
    micBuf = new Uint8Array(micAnalyser.frequencyBinCount);
    micNoise = 0;
    btn.textContent='🎤 Bip ON';
    log('🎤 Détection de bip active — bip = confirmation de réception ventilateur');
    micLoop();
  } catch(e) {
    log('🎤 Micro inaccessible : '+e);
  }
}
let beepBase=0;
function micLoop() {
  if (!micAnalyser) return;
  requestAnimationFrame(micLoop);
  micAnalyser.getByteFrequencyData(micBuf);
  const hzPerBin = audioCtx.sampleRate / micAnalyser.fftSize;
  let band=0, nb=0, peak=0, peakBin=0;
  for (let i=0;i<micBuf.length;i++) {
    const f=i*hzPerBin;
    if (f<1000 || f>6000) continue;
    band+=micBuf[i]; nb++;
    if (micBuf[i]>peak) { peak=micBuf[i]; peakBin=i; }
  }
  band/=Math.max(nb,1);
  if (beepBase===0) beepBase=band;
  beepBase = beepBase*0.98 + band*0.02;
  const now = performance.now();
  const lvl = Math.round(band);
  const inBeep = now < micBeepUntil;
  $('micInfo').textContent =
    `🔊 ${'▁▂▃▄▅▆▇'[Math.min(6,Math.floor(lvl/25))]} ${lvl}/${Math.round(beepBase)} · pic ${Math.round(peakBin*hzPerBin)} Hz${inBeep?' ★':''}`;
  const peakHz = peakBin*hzPerBin;
  const tonal = peakHz >= 900 && peakHz <= 4500;
  let neigh=0, ncnt=0;
  for (let i=Math.max(0,peakBin-10); i<=Math.min(micBuf.length-1,peakBin+10); i++) {
    if (Math.abs(i-peakBin)>2) { neigh+=micBuf[i]; ncnt++; }
  }
  neigh/=Math.max(ncnt,1);
  const strong = peak >= 80;
  const pureTone = peak > neigh*1.3;
  const prominence = band > 20 && band > beepBase*1.4;
  const spike = prominence;
  if (spike && !(tonal && pureTone && strong) && now > micLastBeep+500) {
    log(`… candidat rejeté : ${Math.round(peakHz)} Hz, bande ${lvl}/${Math.round(beepBase)}, pic ${peak} vs vois ${Math.round(neigh)}`);
    micLastBeep = now;
  }
  if (spike && tonal && pureTone && strong && now > micLastBeep+500) {
    micLastBeep = now;
    lastBeepTime = now;
    micBeepUntil = now+250;
    const confirmWeb = (now - lastCmdTime) < 2500;
    const confirmRemote = (now - lastHeardTime) < 1500;
    if (confirmWeb) logEv(`🔊 Bip ${Math.round(peakHz)} Hz — commande confirmée`, 'beep');
    else if (confirmRemote) logEv(`🔊 Bip ${Math.round(peakHz)} Hz — télécommande confirmée`, 'beep');
    else logEv(`🔈 Ton ${Math.round(peakHz)} Hz sans contexte — bip de remise sous tension ?`, 'rx');
    log(confirmWeb
      ? `🔊 BIP détecté (${Math.round(peakHz)} Hz) — commande web confirmée !`
      : confirmRemote
        ? `🔊 BIP détecté (${Math.round(peakHz)} Hz) — télécommande physique confirmée !`
        : `🔈 ton ${Math.round(peakHz)} Hz (ambiant — remise sous tension ventilateur ?)`);
  }
}
$('btnMic').onclick = toggleMic;

// ---------- Verification automatique : chaque bouton doit produire un bip ----------
async function waitBeep(ms) {
  const t0 = performance.now();
  while (performance.now() - t0 < ms) {
    if (lastBeepTime >= t0) return true;
    await new Promise(r=>setTimeout(r, 60));
  }
  return false;
}
$('btnVerify').onclick = async () => {
  if (!micStream) { log('⚠ Active d\'abord le micro (bouton 🎤 Bip)'); return; }
  const slots = Object.keys(learned).map(Number).filter(s=>learned[s]>8 && s<=13).sort((a,b)=>a-b);
  if (!slots.length) { log('⚠ Aucun bouton appris'); return; }
  log(`✅ Vérification de ${slots.length} boutons — regarde le journal...`);
  let ok=0;
  for (const s of slots) {
    log(`→ ${SLOT_NAMES[s]||('slot '+s)}...`);
    await send('play'+s);
    const heard = await waitBeep(1600);
    log(heard ? `   ✅ bip confirmé` : `   ❌ PAS DE BIP — commande non reçue ?`);
    if (heard) ok++;
    await new Promise(r=>setTimeout(r, 500));
  }
  log(`✅ Vérification terminée : ${ok}/${slots.length} boutons confirmés par bip`);
  log('🔄 Remise à zéro : OFF (inerte si déjà éteint)');
  await send('play0');
};
updateWizList();

if (!('serial' in navigator)) {
  document.body.insertAdjacentHTML('afterbegin',
    '<div style="background:#5c2b2b;color:#fff;padding:10px;text-align:center">⚠ Web Serial non supporté — utilise Chrome ou Edge.</div>');
}
