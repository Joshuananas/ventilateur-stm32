// Tests property-based sur le code web reel : sequences aleatoires de lignes
// serie + oracle d'invariants, avec reduction automatique du contre-exemple.
// Les seeds sont fixes pour la reproductibilite ; un echec rapporte seed + cas minimal.
const test = require('node:test');
const assert = require('node:assert/strict');
const {environment} = require('./helpers/web_harness');
const {makeRng, check} = require('./helpers/pbt');

// ---------- construction des lignes serie ----------
const stateLine = o =>
  `[STATE] mode=${o.mode} known=${o.known} power=${o.power} speed=${o.speed} rev=${o.rev} light=${o.light} manual_left_min=${o.left}`;
const learnLine = o =>
  `[LEARN] obs=3/64 mode=SEUILS action=${o.action} raison=${o.reason} eval=${o.evalMin} jour=o hiver=n soleil=n ` +
  `Text=-- seuils=24.0/26.9/29.8 savoir=0 ban=0 ress=-- suspect=n` +
  (o.decision !== null ? ` decision=${o.decision}` : '') +
  (o.evaluating !== null ? ` evaluating=${o.evaluating}` : '');

function lineOf(op) {
  switch (op.k) {
    case 'state': return stateLine(op);
    case 'learn': return learnLine(op);
    case 'boot': return '[BOOT] reset : reset logiciel';
    case 'status': return '[STATUS] mode=AUTO T=23.0C H=55.0% P=1001.0hPa Fan=0/NORMAL obs=3/64';
    case 'heard': return op.line;
    case 'obs': return '>>> Observation #3 enregistree';
    case 'garbage': return op.line;
  }
}

// ---------- oracle : replique exacte du modele attendu ----------
function expected(op, model) {
  switch (op.k) {
    case 'state': {
      const manual = op.mode !== 'AUTO';
      const dir = op.rev === 1 ? 'R' : 'N';
      const s = {manual, known: op.known === 1, power: op.power === 1, speed: op.speed, dir};
      if (model.state && (model.state.manual !== s.manual || model.state.known !== s.known
          || model.state.power !== s.power || model.state.speed !== s.speed || model.state.dir !== s.dir))
        model.learn = null;
      model.state = s;
      break;
    }
    case 'learn':
      model.learn = {decision: op.decision, evaluating: op.evaluating, evalMin: op.evalMin};
      break;
    case 'boot':
      model.state = null; model.learn = null;
      break;
  }
}

function invariants(el, model) {
  const t = id => (el[id] ? String(el[id].textContent) : '');
  const all = ['lrAction', 'lrDecision', 'lrEval', 'lrMode', 'stMode', 'fanResume', 'lrObs', 'lrCtx'].map(t);
  if (all.some(x => /undefined|NaN/.test(x))) return false;
  const st = model.state, lv = model.learn;
  const action = t('lrAction');
  // '' = bloc non encore rendu (le HTML fournit ses propres valeurs initiales).
  if (!st) { if (action && !/attente|inconnu/i.test(action)) return false; }
  else if (!st.known) { if (!/inconnu/i.test(action)) return false; }
  else if (!st.power || st.speed === 0) { if (!/arrêté/.test(action)) return false; }
  else {
    if (!action.includes(`Vitesse ${st.speed}`)) return false;
    if (st.dir === 'R' ? !/inverse/.test(action) : !/normal/.test(action)) return false;
  }
  if (st) {
    if (t('stMode') !== (st.manual ? 'MANUEL' : 'AUTO')) return false;
    if (!/MANUEL|AUTO/.test(t('lrMode'))) return false;
  }
  const dec = t('lrDecision');
  if (!st) { if (dec && !/attente/i.test(dec)) return false; }
  else if (st.manual) { if (!/suspendu/i.test(dec)) return false; }
  else if (!lv) { if (dec && !/attente/i.test(dec)) return false; }
  else if (lv.decision === null) { if (!/indisponible/i.test(dec)) return false; }
  else if (!/^v([0-6])\/([NR])$/.test(lv.decision)) { if (!/Aucune proposition/i.test(dec)) return false; }
  else {
    const v = +lv.decision[1];
    if (v === 0 ? !/arrêté/.test(dec) : !dec.includes(`Vitesse ${v}`)) return false;
  }
  const ev = t('lrEval');
  if (!lv) { if (ev && !/attente/i.test(ev)) return false; }
  else if (lv.evalMin === '-' || lv.evaluating === '-') { if (!/aucune mesure/i.test(ev)) return false; }
  else if (!ev.includes(`~${lv.evalMin}`)) return false;
  return true;
}

// prop : rejoue la sequence et verifie les invariants apres chaque ligne.
function prop(ops) {
  const e = environment();
  const model = {state: null, learn: null};
  try {
    for (const op of ops) {
      e.feed(lineOf(op));
      expected(op, model);
      e.runTimers();
      if (!invariants(e.elements, model)) return false;
    }
  } catch { return false; }
  return true;
}

// ---------- generateurs ----------
const ACTIONS = ['v0/N', 'v1/N', 'v2/N', 'v2/R', 'v4/N', 'v4/R', 'v6/N', 'v6/R'];
const DECISIONS = [...ACTIONS, '-'];
const EVALUATING = [...ACTIONS, '-'];
const EVALMIN = ['-', '5', '10', '12'];
const GARBAGE = [
  '[STATE] mode=AUTO', '[LEARN] obs=??', '[STATUS]', 'ligne brute ???',
  '[STATE] mode=AUTO known=1 power=1 speed=9 rev=1 light=X manual_left_min=-3',
  '[PREF] begin', '[KNOW] 2 cas figes', '\u0000\u0001', '   ',
];
const HEARD = ['[HEARD] slot=-1', '[HEARD] activite telecommande'];

function genOp(rng) {
  switch (rng.int(0, 9)) {
    case 0: case 1: case 2:
      return {k: 'state', mode: rng.pick(['AUTO', 'MANUAL']), known: rng.int(0, 1),
        power: rng.int(0, 1), speed: rng.int(0, 6), rev: rng.int(0, 1),
        light: rng.pick(['OFF', 'WARM', 'NEUTRAL', 'COOL']), left: rng.int(0, 480)};
    case 3: case 4: case 5:
      return {k: 'learn', action: rng.pick(ACTIONS), reason: rng.pick(['seuils fixes', 'appris', 'exploration']),
        evalMin: rng.pick(EVALMIN), decision: rng.pick([...DECISIONS, null]),
        evaluating: rng.pick([...EVALUATING, null])};
    case 6: return {k: 'boot'};
    case 7: return {k: rng.pick(['status', 'obs'])};
    case 8: return {k: 'heard', line: rng.pick(HEARD)};
    default: return {k: 'garbage', line: rng.pick(GARBAGE)};
  }
}

// ---------- tests ----------
test('PBT: les invariants des vues tiennent sur 400 sequences aleatoires', () => {
  const r = check({
    name: 'vues coherentes',
    gen: rng => Array.from({length: rng.int(1, 24)}, () => genOp(rng)),
    prop, runs: 400, seed: 0xC0FFEE,
  });
  assert.equal(r.seed, 0xC0FFEE);
});

test('PBT: sequences centrees sur les transitions MANUEL/AUTO', () => {
  check({
    name: 'transitions mode',
    gen: rng => Array.from({length: rng.int(2, 12)}, () =>
      rng.bool(0.6)
        ? {k: 'state', mode: rng.pick(['AUTO', 'MANUAL']), known: 1, power: 1,
            speed: rng.int(0, 6), rev: rng.int(0, 1), light: 'OFF', left: rng.int(0, 480)}
        : genOp(rng)),
    prop, runs: 300, seed: 0xBADC0DE,
  });
});

test('exhaustif: produit cartesien etat x decision x evaluation', () => {
  const decisions = [null, '-', 'v0/N', 'v3/R', 'v6/N'];
  const evals = [null, '-', 'v2/R'];
  let checked = 0;
  for (const mode of ['AUTO', 'MANUAL'])
    for (const known of [0, 1])
      for (const power of [0, 1])
        for (let speed = 0; speed <= 6; speed++)
          for (const rev of [0, 1])
            for (const decision of decisions)
              for (const evaluating of evals)
                for (const evalMin of ['-', '10']) {
                  const ops = [
                    {k: 'state', mode, known, power, speed, rev, light: 'OFF', left: 120},
                    {k: 'learn', action: 'v2/N', reason: 'seuils fixes', evalMin, decision, evaluating},
                  ];
                  assert(prop(ops), `invariant viole: ${JSON.stringify(ops)}`);
                  checked++;
                }
  assert.equal(checked, 2 * 2 * 2 * 7 * 2 * 5 * 3 * 2);
});
