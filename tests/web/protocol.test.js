const test = require('node:test');
const assert = require('node:assert/strict');
const {environment} = require('./helpers/web_harness');
const messages = require('../fixtures/serial_messages.json');

for (const count of [0, 1, 8, 9, 16, 64]) {
  test(`actual observation parser handles ${count} rows and pagination`, () => {
    const e = environment();
    for (let offset = 0; offset < Math.max(1, count); offset += 8) {
      e.feed(`[OBS] begin count=${count} cap=64 offset=${offset}`);
      const end = Math.min(count, offset + 8);
      for (let i = offset; i < end; i++) e.feed(messages.observation.replace('i=0', `i=${i}`));
      e.feed(`[OBS] end count=${count} next=${end}`);
      e.runTimers();
    }
    assert.equal(e.elements.observationRows.children.length, count);
    assert.deepEqual(e.sent, Array.from({length: Math.max(0, Math.ceil(count / 8) - 1)}, (_, i) => `observations ${(i + 1) * 8}`));
    assert.doesNotMatch(e.elements.observationSummary.textContent, /incomplète/);
    if (count) {
      const cells = e.elements.observationRows.children[0].children;
      assert.equal(cells[1].textContent, 'v2 / inverse');
      assert.equal(cells[2].textContent, '24.00 → 24.10 °C');
      assert.equal(cells[6].textContent, '-0.10 °C');
    }
  });
}

test('actual parser detects a missing observation row', () => {
  const e = environment();
  e.feed('[OBS] begin count=2 cap=64 offset=0');
  e.feed(messages.observation);
  e.feed('[OBS] end count=2 next=2');
  e.runTimers();
  assert.match(e.elements.observationSummary.textContent, /incomplète/);
  assert.deepEqual(e.sent, []);
});
test('actual parser stops pagination if the count changes during transfer', () => {
  const e = environment();
  e.feed('[OBS] begin count=9 cap=64 offset=0');
  for (let i = 0; i < 8; i++) e.feed(messages.observation.replace('i=0', `i=${i}`));
  e.feed('[OBS] end count=10 next=8');
  e.runTimers();
  assert.match(e.elements.observationSummary.textContent, /incomplète/);
  assert.deepEqual(e.sent, []);
});
test('actual parser renders a stopped observation without inventing speed', () => {
  const e = environment();
  e.feed('[OBS] begin count=1 cap=64 offset=0');
  e.feed(messages.observation.replace('v=2 dir=R', 'v=0 dir=N'));
  e.feed('[OBS] end count=1 next=1');
  assert.match(e.elements.observationRows.children[0].children[1].textContent, /Arrêt/);
});
test('actual preference parser handles empty and nonempty snapshots', () => {
  const e = environment();
  e.feed('[PREF] begin'); e.feed('[PREF] end');
  assert.match(e.elements.preferenceList.textContent, /Aucun/);
  e.feed('[PREF] begin'); e.feed(messages.preference); e.feed('[PREF] end');
  assert.equal(e.elements.preferenceList.children.length, 1);
  assert.match(e.elements.preferenceList.children[0].textContent, /inverse.*hiver.*3\/10/);
});
test('preferences and consolidated knowledge stay separate', () => {
  const e = environment();
  e.feed('[PREF] begin'); e.feed(messages.preference); e.feed('[PREF] end');
  e.feed('[KNOW] 1 cas figes'); e.feed(messages.knowledge);
  assert.equal(e.elements.preferenceList.children.length, 1);
  assert.equal(e.ctx.knowList.length, 1);
  assert.equal(e.ctx.knowList[0].dir, 'R');
  assert.equal(e.ctx.knowList[0].d, '-0.10');
});
test('STATUS updates sensor values without replacing tracked STATE', () => {
  const e = environment(); e.feed(messages.state); e.feed(messages.status);
  assert.equal(e.elements.gTemp.textContent, '24.0');
  assert.equal(e.elements.gHum.textContent, '56.0');
  assert.equal(e.elements.gPres.textContent, '1000.0');
  assert.match(e.elements.lrAction.textContent, /inverse/);
});
test('observation completion requests fresh snapshots, not fan commands', () => {
  const e = environment(); e.feed('>>> Observation #1 : action v2R'); e.runTimers();
  assert.deepEqual(e.sent, ['observations', 'savoir']);
});
