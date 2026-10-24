const LIGHT_FR = {OFF:['Éteinte','light-off'], WARM:['Chaud','light-warm'], NEUTRAL:['Neutre','light-neutral'], COOL:['Froid','light-cool']};
let controlIsManual = false;
let trackedFanState = null;
let learningView = null;
function actionText(speed, dir) {
  return +speed === 0 ? 'Ventilateur arrêté'
    : `Vitesse ${speed} — sens ${dir === 'R' ? 'inverse' : 'normal'}`;
}
function resetStateViews() {
  trackedFanState = null;
  learningView = null;
  controlIsManual = false;
  ['stPower','stSpeed','stRev','stLight','gFan','stMode'].forEach(id => { $(id).textContent = '—'; });
  $('stModeSub').textContent = 'mode inconnu';
  $('fanResume').textContent = "En attente d'un nouvel état du STM32…";
  $('unknownBanner').classList.remove('hidden');
  $('rotor').classList.remove('spin', 'rev');
  renderLearningState();
}
function renderLearningState() {
  const state = trackedFanState;
  const info = learningView;
  $('lrAction').textContent = !state ? "En attente de l'état du ventilateur…"
    : !state.known ? 'État du ventilateur inconnu'
    : actionText(state.power ? state.speed : 0, state.dir);
  $('lrMode').textContent = !state ? 'mode inconnu'
    : state.manual ? `MANUEL · maintien restant ${state.left} min` : 'AUTO';
  $('lrMode').className = 'pill ' + (state && state.manual ? 'manual' : 'auto');
  const target = info && info.decision && info.decision.match(/^v([0-6])\/([NR])$/);
  let proposal = !state ? 'En attente du mode de commande…'
    : state.manual ? 'AUTO suspendu : votre réglage est prioritaire.'
    : !info ? 'En attente de la proposition…'
    : info.decision === null ? 'Proposition distincte indisponible : firmware à mettre à jour.'
    : !target ? 'Aucune proposition AUTO calculée depuis la reprise.'
    : actionText(target[1], target[2]);
  if (state && !state.manual && target) {
    const speed = state.power ? state.speed : 0;
    proposal += !state.known ? " · application impossible à comparer : état inconnu"
      : +target[1] !== speed || (speed > 0 && target[2] !== state.dir)
        ? " · cible différente de l'état suivi (application non confirmée)"
        : " · correspond à l'état suivi, sans accusé de réception RF";
  }
  $('lrDecision').textContent = proposal;
  $('lrWhy').textContent = state && state.manual
    ? 'Votre choix est maintenu ; les commandes automatiques sont suspendues.'
    : info ? (REASON_FR[info.reason] || info.reason) : 'En attente…';
  const assessed = info && info.evaluating && info.evaluating.match(/^v([0-6])\/([NR])$/);
  $('lrEval').textContent = !info ? "En attente du statut d'évaluation…"
    : info.evalMin === '-' || info.evaluating === '-' ? "Aucune mesure d'effet en cours."
    : `${assessed ? actionText(assessed[1], assessed[2]) : 'Action évaluée non précisée par ce firmware'} · résultat dans ~${info.evalMin} min.`;
  $('advice').textContent = state && state.manual
    ? 'Réglage utilisateur maintenu : ' + $('lrAction').textContent
    : 'Dernière proposition du programme : ' + proposal;
}
function updateState(m) {
  const [ , mode, known, power, speed, rev, light, left ] = m;
  const auto = mode === 'AUTO';
  controlIsManual = !auto;
  if (trackedFanState && (trackedFanState.manual !== !auto || trackedFanState.known !== (known === '1')
      || trackedFanState.power !== (power === '1') || trackedFanState.speed !== +speed
      || trackedFanState.dir !== (rev === '1' ? 'R' : 'N'))) learningView = null;
  trackedFanState = {manual: !auto, known: known === '1', power: power === '1',
    speed: +speed, dir: rev === '1' ? 'R' : 'N', left: +left};
  renderLearningState();
  $('stMode').textContent = auto ? 'AUTO' : 'MANUEL';
  $('stMode').style.color = auto ? 'var(--ok)' : 'var(--warn)';
  $('stModeSub').textContent = auto ? 'apprentissage actif' : `retour auto dans ${Math.floor(left/60)}h${String(left%60).padStart(2,'0')}`;
  $('modePill').textContent = auto ? 'AUTO' : 'MANUEL';
  $('modePill').className = 'pill ' + (auto ? 'auto' : 'manual');
  $('unknownBanner').classList.toggle('hidden', known === '1');
  const rotor = $('rotor');
  if (known === '1') {
    $('stPower').textContent = power === '1' ? 'ON' : 'OFF';
    $('stPower').style.color = power === '1' ? 'var(--ok)' : 'var(--err)';
    $('stSpeed').textContent = speed === '0' ? '?' : speed;
    $('stRev').textContent = rev === '1' ? '↕ Reverse' : 'Normal';
    const lf = LIGHT_FR[light] || LIGHT_FR.OFF;
    $('stLight').textContent = '💡 ' + lf[0];
    $('stLight').className = lf[1];
    // Rotor : tourne a la vitesse connue, sens inverse en reverse
    const sp = power === '1' ? +speed : 0;
    rotor.classList.toggle('spin', sp > 0);
    rotor.classList.toggle('rev', rev === '1');
    rotor.style.setProperty('--spinDur', sp > 0 ? (3.2 - sp*0.35)+'s' : '0s');
    $('fanResume').textContent = sp === 0
      ? 'Ventilateur à l\u2019arrêt'
      : `Tourne en vitesse ${sp} · ${rev === '1' ? 'souffle vers le plafond' : 'brise vers le bas'} · lumière ${lf[0].toLowerCase()}`;
  } else {
    ['stPower','stSpeed','stRev','stLight'].forEach(id => { $(id).textContent = '?'; $(id).style.color = ''; });
    rotor.classList.remove('spin');
    $('fanResume').textContent = 'État réel inconnu — synchronise ou utilise la vraie télécommande.';
  }
}

// --- Carte "Apprentissage" : traduit la ligne [LEARN] en francais courant ---
let knowList = [];
function renderKnow() {
  const el = $('knowStrip');
  $('dotBrain').textContent = knowList.length ? ` ${knowList.length}●` : '';
  if (!knowList.length) {
    el.innerHTML = '<span class="sub">Aucun pour l\u2019instant — ils apparaîtront après 3 mesures concordantes d\u2019un même réglage.</span>';
    return;
  }
  el.innerHTML = knowList.map(k => {
    const act = k.v === '0' ? 'Arrêt' : `Vitesse ${k.v}`;
    const dir = k.dir === 'R' ? '↕ inverse' : '↓ normal';
    const eff = `écart corrigé ${+k.d > 0 ? '+' : ''}${k.d} °C (estimation)`;
    const confCol = +k.conf >= 70 ? 'var(--ok)' : (+k.conf >= 45 ? 'var(--warn)' : 'var(--err)');
    return `<div class="kchip"><b>💨 ${act}</b> <span style="color:var(--dim)">${dir}</span><br>
      <span style="color:var(--dim)">vers ~${k.t}°C${k.h > 0 ? ` · ${Math.round(k.h)}% HR` : ''}</span><br>
      Effet mesuré : <b>${eff}</b> · ${k.n} obs
      <div class="confwrap"><div class="conf" style="width:${k.conf}%;background:${confCol}"></div></div>
      <span style="font-size:11px;color:var(--dim)">confiance ${k.conf}%</span></div>`;
  }).join('');
}

const REASON_FR = {
  'demarrage':            'Je viens de démarrer, je n\u2019ai pas encore décidé.',
  'reset':                'Mon apprentissage a été remis à zéro.',
  'choix utilisateur':    'Votre réglage est maintenu. Son évaluation continue sans commande automatique.',
  'preference utilisateur': 'Je reprends votre préférence dans des conditions similaires, indépendamment de son effet sur la température.',
  'delai min 90s':        'J\u2019ai envoyé un ordre il y a moins de 90 s : j\u2019attends avant d\u2019en changer.',
  'seuils fixes':         'Je n\u2019ai pas encore assez d\u2019observations : j\u2019applique mes seuils de température par défaut.',
  'appris':               'D\u2019après mes observations passées, c\u2019est le réglage qui a le mieux marché dans des conditions similaires.',
  'appris+humidite':      'Il fait chaud ET humide : d\u2019après mes observations + un cran de plus à cause de l\u2019humidité.',
  'seuils+humidite':      'Il fait chaud ET humide : mes seuils par défaut + un cran de plus à cause de l\u2019humidité.',
  'anticipation soleil':  'Fort soleil sur la baie vitrée : la température va monter, j\u2019anticipe.',
  'confort : brise minimum': 'Il fait chaud : même si la pièce ne refroidit pas, je garde au moins une petite brise pour le confort.',
  'destratification (frais+humide)': 'Il fait frais et humide : je redescends doucement l\u2019air chaud du plafond, sans souffler vers le bas.',
  'hiver : brassage plafond': 'Mode hiver : je brasse l\u2019air chaud accumulé au plafond (chauffage), sans courant d\u2019air direct.',
  'nuit : silence':       'C\u2019est la nuit : je limite la vitesse pour rester silencieux.',
  'aeration : circulation': 'Fenêtre ouverte détectée : je garde une circulation douce pour renouveler l\u2019air.',
  'sens bloque 3min':     'Je viens de changer de sens de rotation : j\u2019attends 3 min avant d\u2019en rechanger.',
  'hysteresis seuil':     'La température est tout près d\u2019un repère : je garde la vitesse actuelle pour éviter de faire des yoyo.',
  'exploration':          'Essai contrôlé (~1 fois / 4 h) : je teste une vitesse peu essayée dans ces conditions pour enrichir mon expérience.',
};
function updateLearn(m, details = {decision: null, evaluating: null}) {
  const [, obs, cap, lmode, speed, dir, reason, evalMin, jour, hiver, soleil, text, lo, med, hi, savoir, ban, ress, suspect, fige, figeTemp, conf] = m;
  learningView = {reason, evalMin, decision: details.decision, evaluating: details.evaluating};
  renderLearningState();
  const figeEl = $('lrFige');
  if (fige && fige !== '-') {
    const n = +fige;
    if (+conf >= 0)
      figeEl.textContent = `✅ Cas figé : v${speed}${dir === 'R' ? ' inverse' : ''} vers ~${figeTemp}°C — confiance ${conf}%`;
    else
      figeEl.textContent = `🧊 ${n}/3 mesures concordantes à ~${figeTemp}°C pour figer ce réglage`;
  } else {
    figeEl.textContent = '—';
  }
  const ctx = [];
  ctx.push(jour === 'o' ? '☀ jour estimé' : '🌙 nuit estimée');
  if (hiver === 'o') ctx.push('❄ saison chauffage');
  if (soleil === 'o') ctx.push('🔆 fort ensoleillement');
  if (text !== '--') ctx.push(`🌡 ${text}°C dehors`);
  if (ress && ress !== '--') ctx.push(`🥵 humidex ${ress} (indicatif, pas une température mesurée)`);
  if (suspect === 'o') ctx.push('⚠ le ventilateur ne semble pas répondre (débranché ? portée ?)');
  $('lrCtx').textContent = ctx.join(' · ');
  $('lrSeuils').textContent = `Repli : arrêt sous ${lo} °C · v2 dès ${lo} °C · v4 dès ${med} °C · v6 dès ${hi} °C. ` +
    (hiver === 'o' ? `Hiver : si actif sous ${med} °C, brassage inverse limité à v2 ; dès ${med} °C, brise normale possible. `
      : 'Hors hiver : sens appris ou normal ; frais/humide peut proposer le sens inverse. ') +
    'Vos préférences priment. Les repères évoluent avec les observations ; ce ne sont pas des températures de confort validées. Humidité, contexte et stabilité peuvent ajuster la proposition.';
  const k = +savoir || 0;
  const bm = +ban || 0;
  const bannedList = [0,1,2,3,4,5,6].filter(s => bm & (1 << s));
  $('lrObs').textContent = `${obs}/${cap} observations · ${lmode === 'APPRIS' ? 'sélection apprise disponible' : 'sélection de repli'}` +
    (bannedList.length ? ` · 🚫 vitesses exclues : ${bannedList.join(', ')}` : '') +
    (k ? ` · ${k} réglage${k>1?'s':''} figé${k>1?'s':''}` : '');
}
