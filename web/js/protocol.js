// ---- Dump en cours : collecte du CSV entre DUMPBEGIN et DUMPEND ----
let dumping = null; // {slot, data:[]}

function handleLine(l) {
  log(l);
  if (l.startsWith('[BOOT] reset :')) { resetStateViews(); return; }
  if (dumping) {
    if (l.includes('DUMPEND')) {
      learned[dumping.slot] = dumping.data.length;
      saveData[dumping.slot] = dumping.data;
      markLearned(dumping.slot);
      dumping = null;
      $('saveInfo').textContent = 'Dump reçu';
      return;
    }
    const nums = l.split(',').map(x=>parseInt(x)).filter(x=>!isNaN(x));
    dumping.data.push(...nums);
    return;
  }
  let m = l.match(/DUMPBEGIN (\d+)/);
  if (m) { dumping = {slot:+m[1], data:[]}; return; }
  if (l === '[PREF] begin') { $('preferenceList').replaceChildren(); return; }
  if (l === '[PREF] end') {
    if (!$('preferenceList').children.length) $('preferenceList').textContent = 'Aucun choix utilisateur mémorisé.';
    return;
  }
  m = l.match(/\[PREF\] v(\d)\/(N|R) temp=(\S+) hum=(\S+) hiver=(\d) poids=(\d+)\/10/);
  if (m) {
    const item = document.createElement('div');
    item.className = 'kchip';
    item.textContent = `${m[1] === '0' ? 'Arrêt' : 'Vitesse ' + m[1]} · ${m[2] === 'R' ? 'inverse' : 'normal'} · ${m[3]} °C / ${m[4]} % HR · ${m[5] === '1' ? 'contexte hiver' : 'hors hiver'} · poids ${m[6]}/10`;
    $('preferenceList').append(item);
    return;
  }
  m = l.match(/\[OBS\] begin count=(\d+) cap=(\d+) offset=(\d+)/);
  if (m) {
    if (+m[3] === 0) {
      observationExpected = +m[1];
      $('observationRows').replaceChildren();
    }
    $('observationSummary').textContent = `Lecture de ${m[1]}/${m[2]} observations…`;
    return;
  }
  m = l.match(/\[OBS\] i=(\d+) v=(\d) dir=(N|R) t0=(\S+) t1=(\S+) hum=(\S+) raw=(\S+) expected=(\S+) effect=(\S+)/);
  if (m) {
    const row = document.createElement('tr');
    const values = [String(+m[1] + 1), `${m[2] === '0' ? 'Arrêt' : 'v' + m[2]} / ${m[3] === 'R' ? 'inverse' : 'normal'}`,
      `${m[4]} → ${m[5]} °C`, `${m[6]} %`, `${m[7]} °C`, `${m[8]} °C`, `${m[9]} °C`];
    for (const value of values) { const cell = document.createElement('td'); cell.textContent = value; row.append(cell); }
    $('observationRows').append(row);
    return;
  }
  m = l.match(/\[OBS\] end count=(\d+) next=(\d+)/);
  if (m) {
    const received = $('observationRows').children.length;
    if (+m[1] === observationExpected && +m[2] < +m[1] && received === +m[2]) {
      const next = +m[2];
      setTimeout(()=>send('observations ' + next), 150);
      return;
    }
    $('observationSummary').textContent = received === observationExpected && received === +m[1]
      ? (received ? `${received} observations reçues — état en RAM ; sauvegarde flash périodique.` : 'Aucune observation terminée. Un choix peut être mémorisé avant la fin de son évaluation.')
      : `Lecture incomplète (${received}/${m[1]}) : cliquer sur Actualiser.`;
    return;
  }
  m = l.match(/\[STATE\] mode=(\w+) known=(\d) power=(\d) speed=(\d) rev=(\d) light=(\w+) manual_left_min=(\d+)/);
  if (m) { updateState(m); return; }
  if (l.includes('[HEARD]')) lastHeardTime = performance.now();
  m = l.match(/\[HEARD\] activite telecommande/);
  if (m) {
    logEv('📻 Télécommande physique (fragment — bouton non identifié)');
    $('wizMsg').innerHTML = `📻 Télécommande physique utilisée → mode MANUEL <i>(fragment trop court pour identifier le bouton)</i>`;
    flashTab('brain');
    return;
  }
  m = l.match(/\[HEARD\] slot=(-?\d+)/);
  if (m) {
    const s = +m[1];
    if (s >= 0) {
      const b = document.querySelector(`button[data-slot="${s}"]`);
      if (b) { b.classList.add('heard'); setTimeout(()=>b.classList.remove('heard'), 600); }
      logEv(`📻 Télécommande : ${SLOT_NAMES[s]||('slot '+s)}`);
      $('wizMsg').innerHTML = `📻 Télécommande : <b>${SLOT_NAMES[s]||('slot '+s)}</b> détecté → mode MANUEL`;
    } else {
      $('wizMsg').innerHTML = `📻 Fragment RF reçu (signal trop court pour identifier le bouton)`;
    }
    return;
  }
  // "[KNOW] 3 cas figes" -> reset de la liste ; "[KNOW] i=0 v2/N ..." -> chip
  m = l.match(/\[KNOW\] (\d+) cas figes/);
  if (m) { knowList = []; if (+m[1] === 0) renderKnow(); return; }
  m = l.match(/\[KNOW\] i=(\d+) v(\d)\/(\S) temp=([\d.-]+) hum=([\d.-]+) delta=([\d.-]+) n=(\d+) conf=(\d+)/);
  if (m) { knowList.push({v:m[2], dir:m[3], t:m[4], h:m[5], d:m[6], n:m[7], conf:m[8]}); renderKnow(); return; }
  if (l.startsWith('>>> Observation #')) {
    setTimeout(()=>send('observations'), 500);
    setTimeout(()=>send('savoir'), 900);
  }
  // Rafraichit la liste quand une connaissance est figee/defigee
  if (l.includes('>>> Nouvelle connaissance') || l.includes('DEFIGEE')) {
    setTimeout(()=>send('savoir'), 500);
    flashTab('brain');
    return;
  }
  m = l.match(/\[LEARN\] obs=(\d+)\/(\d+) mode=(\S+) action=v(\d)\/(\S) raison=(.+?) eval=(\S+) jour=(\S) hiver=(\S) soleil=(\S) Text=(\S+) seuils=([\d.]+)\/([\d.]+)\/([\d.]+)(?: savoir=(\d+))?(?: ban=(\d+))?(?: ress=(\S+))?(?: suspect=(\S))?(?: fige=(\S+) figetemp=(\S+) conf=(-?\d+))?/);
  if (m) {
    const decision = l.match(/ decision=(v[0-6]\/[NR]|-)(?:\s|$)/);
    const evaluating = l.match(/ evaluating=(v[0-6]\/[NR]|-)(?:\s|$)/);
    updateLearn(m, {decision: decision ? decision[1] : null, evaluating: evaluating ? evaluating[1] : null});
    return;
  }
  m = l.match(/\[STATUS\] mode=(\S+) T=([\d.-]+)C H=([\d.]+)% P=([\d.]+)hPa Fan=(\S+) obs=(\d+\/\d+)/);
  if (m) {
    $('modePill').textContent=m[1];
    const T = +m[2];
    $('gTemp').textContent=m[2]; $('gHum').textContent=m[3];
    $('gPres').textContent=m[4]; $('gFan').textContent=m[5];
    $('obs').textContent=m[6];
    const gq=$('gqT'); gq.classList.toggle('hot', T>=28); gq.classList.toggle('warm', T>=25 && T<28);
    return;
  }
  // "[CTX] cuisson/aeration" -> remonte l'evenement dans les detections
  if (l.includes('[CTX]')) { logEv('🧭 '+l.replace('[CTX] ',''), 'beep'); return; }
  // Fin de capture : "[RF] 512 transitions capturees"
  if (l.match(/\[RF\] \d+ transitions capturees/)) {
    const n = parseInt(l.match(/(\d+)/)[1]);
    finishLearn(n);
    return;
  }
  // "[RF] slot=3 pulses=512"
  m = l.match(/\[RF\] slot=(\d+) pulses=(\d+)/);
  if (m) {
    learned[m[1]] = +m[2];
    markLearned(+m[1]);
    return;
  }
  // "[FLASH] sauvegarde OK" etc.
  m = l.match(/\[FLASH\] (.+)/);
  if (m) { $('saveInfo').textContent = '⚡ '+m[1]; return; }
  // liste "slots"
  m = l.match(/\[RF\] slot (\d+) : (\d+)/);
  if (m && +m[2] > 8) { learned[m[1]] = +m[2]; markLearned(+m[1]); }
}

// Petit flash visuel sur un onglet quand un evenement s'y produit
function flashTab(tab) {
  const b = document.querySelector(`nav.tabs button[data-tab="${tab}"]`);
  if (b && !b.classList.contains('active')) {
    b.style.transition='none'; b.style.color='var(--warn)';
    setTimeout(()=>{ b.style.transition=''; b.style.color=''; }, 1200);
  }
}

