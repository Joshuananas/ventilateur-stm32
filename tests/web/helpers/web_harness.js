const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const assert = require('node:assert/strict');

const root = path.resolve(__dirname, '../../..');
const html = fs.readFileSync(path.join(root, 'web/index.html'), 'utf8');
const scriptFiles = [...html.matchAll(/<script src="([^"]+)"><\/script>/g)].map(m => m[1]);
assert.deepEqual(scriptFiles, ['js/core.js', 'js/protocol.js', 'js/views.js', 'js/actions.js'],
  'web/index.html must load the four modules in order');
const parts = scriptFiles.map(f => {
  const source = fs.readFileSync(path.join(root, 'web', f), 'utf8');
  new vm.Script(source, {filename: 'web/' + f});
  return source;
});
const script = parts.join('\n');

function sourceBetween(start, end) {
  const from = script.indexOf(start);
  const to = script.indexOf(end, from);
  assert(from >= 0 && to > from, `Missing source boundaries: ${start} / ${end}`);
  return script.slice(from, to);
}

function environment() {
  const elements = {};
  const sent = [];
  const scheduled = [];
  function element() {
    return {
      textContent: '', innerHTML: '', className: '', children: [],
      style: {setProperty() {}},
      classList: {toggle() {}, remove() {}, add() {}},
      append(child) { this.children.push(child); },
      replaceChildren() { this.children = []; this.textContent = ''; },
    };
  }
  const ctx = vm.createContext({
    $: id => elements[id] || (elements[id] = element()),
    document: {createElement: element, querySelector: () => null},
    log() {}, logEv() {}, dumping: null, observationExpected: 0, lastHeardTime: 0,
    learned: {}, saveData: {}, markLearned() {}, SLOT_NAMES: {}, finishLearn() {},
    performance: {now: () => 0},
    setTimeout(fn) { scheduled.push(fn); },
    send(command) { sent.push(command); },
    flashTab() {}, renderKnow() {}, knowList: [],
  });
  vm.runInContext(sourceBetween('const LIGHT_FR =', '// --- Carte "Apprentissage"'), ctx);
  vm.runInContext(sourceBetween('const REASON_FR =', "$('btnSync').onclick"), ctx);
  vm.runInContext(sourceBetween('function handleLine(l)', '// Petit flash visuel'), ctx);
  return {
    ctx, elements, sent,
    feed: line => ctx.handleLine(line),
    runTimers() { for (const fn of scheduled.splice(0)) fn(); },
  };
}

module.exports = {root, html, script, environment};
