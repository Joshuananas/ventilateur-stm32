const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const test = require('node:test');
const assert = require('node:assert/strict');
const {root, script} = require('./helpers/web_harness');
const contract = require('../fixtures/firmware_contract.json');
const read = name => fs.readFileSync(path.join(root, name), 'utf8');
const main = read('src/main.cpp');
const controllerSource = read('src/controller.cpp');
const learningHeader = read('src/learning.h');
const learningSource = read('src/learning.cpp');
const learningStore = read('src/learning_store.cpp');
const storageHeader = read('src/storage.h');
const storageSource = read('src/storage.cpp');
const radioHeader = read('src/cc1101.h');
const limitsHeader = read('src/domain/limits.h');
const fanTypesHeader = read('src/domain/fan_types.h');
const learningTypesHeader = read('src/domain/learning_types.h');
const buttonsHeader = read('src/domain/remote_buttons.h');
const formatHeader = read('src/persistence/storage_format.h');
const flashHeader = read('src/persistence/flash_layout.h');
function constant(source, name) {
  const match = source.match(new RegExp('\\b' + name + '\\s*(?:=\\s*)?(0x[0-9a-fA-F]+|[0-9]+)[uUlL]*\\b'));
  assert(match, 'missing numeric constant ' + name);
  return Number(match[1]);
}
const uniqueSorted = values => [...new Set(values)].sort();

test('reference command names and prefixes are retained', () => {
  assert.deepEqual(uniqueSorted([...controllerSource.matchAll(/cmd == "([^"]+)"/g)].map(m => m[1])), uniqueSorted(contract.exactCommands));
  assert.deepEqual(uniqueSorted([...controllerSource.matchAll(/cmd\.startsWith\("([^"]+)"\)/g)].map(m => m[1])), uniqueSorted(contract.commandPrefixes));
  assert.match(controllerSource, /cmd\[0\] == 's' && cmd\[1\] >= '1' && cmd\[1\] <= '6'/);
  assert.deepEqual(contract.speedCommands, ['s1', 's2', 's3', 's4', 's5', 's6']);
});
test('RF and learning capacities and format identifiers are retained', () => {
  assert.equal(constant(limitsHeader, 'MAX_SLOTS'), contract.radio.slots);
  assert.equal(constant(limitsHeader, 'MAX_PULSES'), contract.radio.pulsesPerSlot);
  assert.equal(constant(limitsHeader, 'MAX_BITS'), contract.radio.decodedBits);
  assert.equal(constant(limitsHeader, 'CAPACITY'), contract.learning.observations);
  assert.equal(constant(limitsHeader, 'KNOW_CAP'), contract.learning.knowledge);
  for (const name of ['MAX_SLOTS', 'MAX_PULSES', 'MAX_BITS'])
    assert.match(radioHeader, new RegExp('\\b' + name + '\\s*=\\s*RadioLimits::' + name + ';'));
  for (const name of ['CAPACITY', 'KNOW_CAP'])
    assert.match(learningHeader, new RegExp('\\b' + name + '\\s*=\\s*LearningLimits::' + name + ';'));
  assert.equal(constant(formatHeader, 'LEARN_MAGIC'), Number(contract.learning.magic));
  assert.equal(constant(formatHeader, 'LEARN_VER'), contract.learning.version);
  assert.equal(constant(formatHeader, 'LEARN_PREFERENCE_MAGIC'), Number(contract.learning.preferenceMagic));
  assert(learningStore.includes('LEARN_PREFERENCE_MAGIC'));
  assert.equal(constant(formatHeader, 'CRC32_POLYNOMIAL'), 0xEDB88320);
  for (const source of [learningStore, storageSource]) assert(source.includes('CRC32_POLYNOMIAL'));
});
test('current flash addresses and boot record identifiers are retained', () => {
  for (const [name, value] of Object.entries({
    STORAGE_ADDR: contract.flash.storageAddress, STORAGE_B_ADDR: contract.flash.storageBAddress,
    COPY_MAGIC: contract.flash.copyMagic, STORAGE_MAGIC: contract.flash.storageMagic,
    STORAGE_VER: contract.flash.storageVersion, BOOTLOG_BASE: contract.flash.bootlogBase,
    BOOTLOG_END: contract.flash.bootlogEnd, BOOTREC_MAGIC: contract.flash.bootRecordMagic,
    BOOTREC_SIZE: contract.flash.bootRecordSize, BOOTLOG_HDR: contract.flash.bootHeaderMagic,
  })) assert.equal(constant(flashHeader + '\n' + formatHeader, name), Number(value), name);
});
for (const [name, declaration] of Object.entries(contract.structures)) {
  test(`persistent source declaration ${name} is retained`, () => {
    const source = name === 'StoredPreference' ? learningTypesHeader : formatHeader;
    const match = source.match(new RegExp('\\b' + name + '\\s*\\{([\\s\\S]*?)\\}'));
    assert(match, name);
    const normalize = text => text.replace(/\/\/[^\n]*/g, '').replace(/\s/g, '')
      .replace(/RadioLimits::/g, 'CC1101::').replace(/LearningLimits::/g, 'FanLearner::');
    assert.equal(normalize(match[1]), normalize(declaration));
    if (name !== 'StoredPreference') assert.match(source, new RegExp('struct __attribute__\\(\\(packed\\)\\) ' + name));
  });
}
test('shared remote identifiers preserve all existing RF slots', () => {
  for (const [name, value] of Object.entries({
    OFF: 0, SPEED_FIRST: 1, SPEED_LAST: 6, LIGHT: 7, DIRECTION: 8,
    BRIGHTNESS_DOWN: 9, BRIGHTNESS_UP: 10, TIMER_1H: 11, TIMER_3H: 12, TIMER_6H: 13,
  })) assert.equal(constant(buttonsHeader, name), value, name);
  assert.match(main, /#define COMBO_SLOT\s+RemoteButton::TIMER_1H/);
  const fanStateSource = read('src/fanstate.cpp');
  for (const name of ['OFF', 'SPEED_FIRST', 'SPEED_LAST', 'DIRECTION', 'LIGHT'])
    assert(fanStateSource.includes('RemoteButton::' + name));
});
test('shared action and light types retain their values and fields', () => {
  assert.equal(constant(fanTypesHeader, 'DIR_NORMAL'), 0);
  assert.equal(constant(fanTypesHeader, 'DIR_REVERSE'), 1);
  assert.match(fanTypesHeader, /LIGHT_OFF\s*=\s*0,\s*LIGHT_WARM,\s*LIGHT_NEUTRAL,\s*LIGHT_COOL/);
  const normalize = text => text.replace(/\/\/[^\n]*/g, '').replace(/\s/g, '');
  for (const [name, expected, source] of [
    ['FanAction', 'uint8_t speed; FanDirection dir;', fanTypesHeader],
    ['Decision', 'FanAction action; bool changed; const char* reason;', fanTypesHeader],
    ['Observation', 'uint32_t timestamp; FanAction action; float tempBefore; float tempAfter; float humBefore; float delta; float expected;', learningTypesHeader],
  ]) {
    const body = source.match(new RegExp('\\b' + name + '\\s*\\{([\\s\\S]*?)\\}'));
    assert(body, name);
    assert.equal(normalize(body[1]), normalize(expected), name);
  }
});
test('extracted definitions do not import hardware drivers or application classes', () => {
  for (const source of [fanTypesHeader, learningTypesHeader, limitsHeader, buttonsHeader, formatHeader, flashHeader])
    assert.doesNotMatch(source, /#include\s*[<"](?:\.\.\/)*(?:Arduino|Wire|SPI|learning\.h|cc1101\.h|stm32)/);
  assert(storageHeader.includes('persistence/storage_format.h'));
});
test('browser remote slot labels retain their button correspondence', () => {
  const match = script.match(/const SLOT_NAMES\s*=\s*(\{[\s\S]*?\});/);
  assert(match);
  const names = vm.runInNewContext('(' + match[1] + ')');
  assert.deepEqual(Object.keys(names).sort(), Object.keys(contract.remoteLabels).sort());
  for (const [slot, label] of Object.entries(contract.remoteLabels)) assert(names[slot].includes(label), slot);
});
test('existing RF export remains a valid pulse export rather than learner storage', () => {
  const data = JSON.parse(read('telecommande.json'));
  assert(Object.keys(data).length > 0);
  for (const [slot, pulses] of Object.entries(data)) {
    assert(/^\d+$/.test(slot) && +slot < contract.radio.slots);
    assert(Array.isArray(pulses) && pulses.length <= contract.radio.pulsesPerSlot);
    for (const pulse of pulses) assert(Number.isInteger(pulse) && pulse >= 0 && pulse <= 65535);
  }
});
test('PlatformIO target and flash limit protect reserved sectors', () => {
  const ini = read('platformio.ini');
  assert.match(ini, /board\s*=\s*blackpill_f411ce/);
  assert.match(ini, /framework\s*=\s*arduino/);
  assert.match(ini, /upload_protocol\s*=\s*dfu/);
  assert.equal(+ini.match(/board_upload\.maximum_size\s*=\s*(\d+)/)[1], contract.flash.currentUploadMaximum);
  assert(contract.flash.currentUploadMaximum <= contract.flash.requiredFirmwareMaximum);
});
