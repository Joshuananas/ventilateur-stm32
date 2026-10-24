// Property-based testing minimal, sans dependance :
// PRNG seedable (mulberry32) + reduction du contre-exemple par suppression.
// Un test PBT retourne {seed, runs} ; rejouer avec {seed} pour reproduire.
const assert = require('node:assert/strict');

function mulberry32(a) {
  return function () {
    a |= 0; a = a + 0x6D2B79F5 | 0;
    let t = Math.imul(a ^ a >>> 15, 1 | a);
    t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
    return ((t ^ t >>> 14) >>> 0) / 4294967296;
  };
}

function makeRng(seed) {
  const f = mulberry32(seed >>> 0);
  return {
    int: (lo, hi) => lo + Math.floor(f() * (hi - lo + 1)),
    pick: arr => arr[Math.floor(f() * arr.length)],
    bool: (p = 0.5) => f() < p,
  };
}

// prop(seq) doit renvoyer true si l'invariant tient, false sinon.
// Reduction : retire des elements tant que la sequence continue d'echouer.
function shrink(seq, prop) {
  let s = seq.slice();
  for (let guard = 0; guard < 300; guard++) {
    let reduced = false;
    for (let i = 0; i < s.length; i++) {
      const t = s.slice(0, i).concat(s.slice(i + 1));
      if (!prop(t)) { s = t; reduced = true; break; }
    }
    if (!reduced) return s;
  }
  return s;
}

function check({name, gen, prop, runs = 200, seed}) {
  seed = seed === undefined ? (Date.now() >>> 0) : seed >>> 0;
  const rng = makeRng(seed);
  for (let i = 0; i < runs; i++) {
    const candidate = gen(rng);
    if (!prop(candidate)) {
      const minimal = shrink(candidate, prop);
      const error = new assert.AssertionError({
        message: `${name} : invariant viole (seed=${seed}, run=${i}, ${candidate.length} ops -> ${minimal.length} apres reduction)\ncontre-exemple minimal : ${JSON.stringify(minimal)}`,
      });
      error.seed = seed;
      error.counterexample = minimal;
      throw error;
    }
  }
  return {seed, runs};
}

module.exports = {makeRng, check, shrink};
