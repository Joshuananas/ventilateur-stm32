const test = require('node:test');
const assert = require('node:assert/strict');
const {html, script, environment} = require('./helpers/web_harness');
const messages = require('../fixtures/serial_messages.json');

const state = (mode = 'MANUAL', rev = 1, known = 1, speed = 2, power = 1) =>
  messages.state.replace('mode=MANUAL', `mode=${mode}`).replace('rev=1', `rev=${rev}`)
    .replace('known=1', `known=${known}`).replace('speed=2', `speed=${speed}`).replace('power=1', `power=${power}`);
const learn = (action = 'v2/N', extra = ' decision=v2/N evaluating=v2/R', evalMin = '10') =>
  messages.legacyLearn.replace('action=v2/N', `action=${action}`).replace('eval=10', `eval=${evalMin}`) + extra;

test('MANUAL STATE then contradictory LEARN preserves tracked direction', () => {
  const e = environment(); e.feed(state()); e.feed(learn());
  assert.match(e.elements.lrAction.textContent, /inverse/);
  assert.doesNotMatch(e.elements.lrAction.textContent, /normal/);
});
test('LEARN before STATE gives the same final tracked state', () => {
  const e = environment(); e.feed(learn()); e.feed(state());
  assert.match(e.elements.lrAction.textContent, /inverse/);
  assert.match(e.elements.lrDecision.textContent, /suspendu/i);
});
test('AUTO separates tracked action from unapplied target', () => {
  const e = environment(); e.feed(state('AUTO')); e.feed(learn());
  assert.match(e.elements.lrAction.textContent, /inverse/);
  assert.match(e.elements.lrDecision.textContent, /normal/);
  assert.match(e.elements.lrDecision.textContent, /diff|non appliqu/i);
});
test('evaluation uses pending action rather than decision', () => {
  const e = environment(); e.feed(state()); e.feed(learn());
  assert.match(e.elements.lrEval.textContent, /inverse/);
  assert.match(e.elements.lrEval.textContent, /10/);
});
test('unknown state is never invented from LEARN', () => {
  const e = environment(); e.feed(state('AUTO', 1, 0)); e.feed(learn());
  assert.match(e.elements.lrAction.textContent, /inconnu/i);
});
test('OFF does not claim airflow and has no pending evaluation', () => {
  const e = environment(); e.feed(state('AUTO', 1, 1, 0, 0));
  e.feed(learn('v0/N', ' decision=v0/N evaluating=-', '-'));
  assert.match(e.elements.lrAction.textContent, /arrêt/);
  assert.doesNotMatch(e.elements.lrAction.textContent, /brise|souffle/);
  assert.match(e.elements.lrEval.textContent, /aucune/i);
});
test('legacy LEARN does not overwrite STATE', () => {
  const e = environment(); e.feed(state()); e.feed(messages.legacyLearn);
  assert.match(e.elements.lrAction.textContent, /inverse/);
  assert.match(e.elements.lrDecision.textContent, /suspendu/i);
});
test('before first STATE tracked state remains unavailable', () => {
  const e = environment(); e.feed(learn());
  assert.match(e.elements.lrAction.textContent, /attente|inconnu/i);
});
test('manual mode hides a stale AUTO target', () => {
  const e = environment(); e.feed(state('AUTO')); e.feed(learn()); e.feed(state());
  assert.match(e.elements.lrDecision.textContent, /suspendu/i);
});
test('failed application has no fictitious pending evaluation', () => {
  const e = environment(); e.feed(state('AUTO')); e.feed(learn('v2/R', ' decision=v4/N evaluating=-', '-'));
  assert.match(e.elements.lrAction.textContent, /inverse/);
  assert.match(e.elements.lrDecision.textContent, /4/);
  assert.match(e.elements.lrEval.textContent, /aucune/i);
});
test('return to AUTO waits for a fresh decision', () => {
  const e = environment(); e.feed(state('AUTO')); e.feed(learn()); e.feed(state()); e.feed(state('AUTO'));
  assert.match(e.elements.lrDecision.textContent, /attente/i);
});
test('physical action change clears stale evaluation', () => {
  const e = environment(); e.feed(state()); e.feed(learn()); e.feed(state('MANUAL', 0));
  assert.match(e.elements.lrEval.textContent, /attente/i);
  assert.match(e.elements.lrAction.textContent, /normal/);
});
test('reset announcement clears both state views', () => {
  const e = environment(); e.feed(state()); e.feed(learn()); e.feed('[BOOT] reset : reset logiciel');
  assert.match(e.elements.lrAction.textContent, /attente/i);
  assert.equal(e.elements.stRev.textContent, '—');
});
test('literal DOM ids referenced by the script exist exactly once', () => {
  const ids = [...html.matchAll(/\bid="([^"]+)"/g)].map(m => m[1]);
  assert.equal(new Set(ids).size, ids.length, 'duplicate HTML id');
  for (const m of script.matchAll(/\$\('([^']+)'\)/g)) assert(ids.includes(m[1]), 'missing id ' + m[1]);
});
