// engine.js — clean-room pattern engine (Strudel-compatible syntax).
//
// This is a fresh implementation, NOT derived from Strudel's source. The
// public API mirrors TidalCycles/Strudel conventions (mini notation, Pattern
// class with .fast/.slow/.rev/.every, queryArc returning Haps) so user-
// written snippets that stick to the documented pattern DSL run here too.
//
// Time model:
//   - A "cycle" is 1 tempo-independent unit of pattern time.
//   - queryArc(begin, end) returns all Haps active within cycle range.
//   - Each Hap is { whole: {begin, end}, part: {begin, end}, value }.
//
// Anything this file doesn't implement is explicitly outside step-2 scope;
// it will grow as later steps wire audio + effects.

// ── TimeSpan + Hap ────────────────────────────────────────────────────
function span(begin, end) { return { begin, end }; }

function intersect(a, b) {
    const lo = Math.max(a.begin, b.begin);
    const hi = Math.min(a.end,   b.end);
    return (lo < hi) ? span(lo, hi) : null;
}

function hap(whole, part, value) { return { whole, part, value }; }

// ── Pattern ───────────────────────────────────────────────────────────
// A Pattern wraps a `query(span) -> Hap[]` function. Combinators produce
// new Patterns by composing queries.
class Pattern {
    constructor(query) { this.query = query; }

    queryArc(begin, end) { return this.query(span(begin, end)); }

    // Attach a value onto every Hap — used to chain .s(), .note(), .gain()
    // style annotations from method calls.
    withValue(f) {
        return new Pattern(sp =>
            this.query(sp).map(h => hap(h.whole, h.part, f(h.value))));
    }

    // Merge named field into each Hap.value (which is assumed object).
    set(k, v) { return this.withValue(val =>
        Object.assign({}, (val && typeof val === 'object') ? val : { value: val }, { [k]: v })); }

    // Named annotations — common enough to alias.
    s(name)    { return this.set('s',    name); }
    note(n)    { return this.set('note', n);    }
    gain(g)    { return this.set('gain', g);    }
    pan(p)     { return this.set('pan',  p);    }
    speed(s)   { return this.set('speed', s);   }
    // Placeholders for effects — recorded on the Hap value so the audio
    // stage can consume them. No DSP here yet.
    lpf(v)     { return this.set('lpf',  v); }
    hpf(v)     { return this.set('hpf',  v); }
    bpf(v)     { return this.set('bpf',  v); }
    delay(v)   { return this.set('delay', v); }
    room(v)    { return this.set('room',  v); }
    crush(v)   { return this.set('crush', v); }
    channel(c) { return this.set('channel', c); }
    // ADSR (seconds for a/d/r; 0..1 for s). Strudel uses the same names.
    attack(v)  { return this.set('attack',  v); }
    decayT(v)  { return this.set('decayT',  v); }   // 'decay' is taken by dyn/damping — alias
    sustain(v) { return this.set('sustain', v); }
    release(v) { return this.set('release', v); }

    // Time-scale: fast(2) compresses two cycles into one.
    fast(factor) {
        return new Pattern(sp => {
            const scaled = span(sp.begin * factor, sp.end * factor);
            return this.query(scaled).map(h => hap(
                span(h.whole.begin / factor, h.whole.end / factor),
                span(h.part.begin  / factor, h.part.end  / factor),
                h.value));
        });
    }
    slow(factor) { return this.fast(1 / factor); }

    // Reverse time within each cycle.
    rev() {
        return new Pattern(sp => {
            const out = [];
            const startCyc = Math.floor(sp.begin);
            const endCyc   = Math.ceil(sp.end);
            for (let c = startCyc; c < endCyc; c++) {
                const local = span(c, c + 1);
                const clip  = intersect(local, sp);
                if (!clip) continue;
                // Query the reflected span, then reflect the Haps back.
                const refl = span(2 * c + 1 - clip.end, 2 * c + 1 - clip.begin);
                const haps = this.query(refl);
                for (const h of haps) {
                    const rw = span(2 * c + 1 - h.whole.end, 2 * c + 1 - h.whole.begin);
                    const rp = span(2 * c + 1 - h.part.end,  2 * c + 1 - h.part.begin);
                    out.push(hap(rw, rp, h.value));
                }
            }
            return out;
        });
    }

    // Apply a function every N cycles, leave the others alone.
    every(n, f) {
        const modded = f(this);
        return new Pattern(sp => {
            const out = [];
            const startCyc = Math.floor(sp.begin);
            const endCyc   = Math.ceil(sp.end);
            for (let c = startCyc; c < endCyc; c++) {
                const local = intersect(span(c, c + 1), sp);
                if (!local) continue;
                const chosen = ((c % n) === 0) ? modded : this;
                out.push(...chosen.query(local));
            }
            return out;
        });
    }

    // Apply f with the given probability (deterministic: uses cycle number
    // as seed so each cycle is consistent).
    sometimesBy(prob, f) {
        const modded = f(this);
        return new Pattern(sp => {
            const out = [];
            const startCyc = Math.floor(sp.begin);
            const endCyc   = Math.ceil(sp.end);
            for (let c = startCyc; c < endCyc; c++) {
                const local = intersect(span(c, c + 1), sp);
                if (!local) continue;
                // Simple integer hash for determinism.
                const h = ((c * 2654435761) >>> 0) / 4294967296;
                const chosen = (h < prob) ? modded : this;
                out.push(...chosen.query(local));
            }
            return out;
        });
    }
    sometimes(f)  { return this.sometimesBy(0.5, f); }
    rarely(f)     { return this.sometimesBy(0.25, f); }
    often(f)      { return this.sometimesBy(0.75, f); }
    always(f)     { return this.sometimesBy(1.0, f); }
    never(_)      { return this; }
}

// ── Primitive constructors ────────────────────────────────────────────
// Silence — emits nothing.
function silence() { return new Pattern(_ => []); }

// Pure — a single value that fills each cycle exactly once.
function pure(value) {
    return new Pattern(sp => {
        const out = [];
        const startCyc = Math.floor(sp.begin);
        const endCyc   = Math.ceil(sp.end);
        for (let c = startCyc; c < endCyc; c++) {
            const whole = span(c, c + 1);
            const p = intersect(whole, sp);
            if (p) out.push(hap(whole, p, value));
        }
        return out;
    });
}

// Sequence n patterns equally within each cycle.
function fastcat(...pats) {
    if (pats.length === 0) return silence();
    const n = pats.length;
    return new Pattern(sp => {
        const out = [];
        const startCyc = Math.floor(sp.begin);
        const endCyc   = Math.ceil(sp.end);
        for (let c = startCyc; c < endCyc; c++) {
            for (let i = 0; i < n; i++) {
                const slotBegin = c + i / n;
                const slotEnd   = c + (i + 1) / n;
                const slotSpan  = span(slotBegin, slotEnd);
                const clip      = intersect(slotSpan, sp);
                if (!clip) continue;
                // Query the sub-pattern in its own normalized [c, c+1) space,
                // then remap back into the slot.
                const rebased = span(c + (clip.begin - slotBegin) * n,
                                     c + (clip.end   - slotBegin) * n);
                const inner = pats[i].query(rebased);
                for (const h of inner) {
                    const wb = slotBegin + (h.whole.begin - c) / n;
                    const we = slotBegin + (h.whole.end   - c) / n;
                    const pb = slotBegin + (h.part.begin  - c) / n;
                    const pe = slotBegin + (h.part.end    - c) / n;
                    out.push(hap(span(wb, we), span(pb, pe), h.value));
                }
            }
        }
        return out;
    });
}

// Alternate — one pattern per cycle, round-robin.
function slowcat(...pats) {
    if (pats.length === 0) return silence();
    const n = pats.length;
    return new Pattern(sp => {
        const out = [];
        const startCyc = Math.floor(sp.begin);
        const endCyc   = Math.ceil(sp.end);
        for (let c = startCyc; c < endCyc; c++) {
            const idx = ((c % n) + n) % n;
            const local = intersect(span(c, c + 1), sp);
            if (!local) continue;
            out.push(...pats[idx].query(local));
        }
        return out;
    });
}

// Stack — parallel patterns within the same cycles.
function stack(...pats) {
    return new Pattern(sp => {
        const out = [];
        for (const p of pats) out.push(...p.query(sp));
        return out;
    });
}

// ── Mini notation parser ──────────────────────────────────────────────
// Grammar (minimal subset):
//   seq     = elem (WS elem)*
//   elem    = atom | group | alt
//   atom    = ident (modifier)*
//   group   = '[' seq ']'
//   alt     = '<' seq '>'
//   modifier= '*' NUMBER | '/' NUMBER | ':' NUMBER
//   ident   = [a-zA-Z0-9._-]+  |  '~'  (silence)
//
// Returns a Pattern. Any value produced wears an implicit string — the
// caller (s(), note()) reinterprets it as needed.
function parseMini(input) {
    const src = String(input).trim();
    let i = 0;

    function skipWs() {
        while (i < src.length && /\s/.test(src[i])) i++;
    }
    function readIdent() {
        const start = i;
        while (i < src.length && /[a-zA-Z0-9._\-#]/.test(src[i])) i++;
        return src.slice(start, i);
    }
    function readNumber() {
        const start = i;
        if (src[i] === '-') i++;
        while (i < src.length && /[0-9.]/.test(src[i])) i++;
        return parseFloat(src.slice(start, i));
    }
    function parseElem() {
        skipWs();
        if (i >= src.length) return null;
        let pat;
        if (src[i] === '[') {
            i++;
            pat = parseSeq(']');
            if (src[i] === ']') i++;
        } else if (src[i] === '<') {
            i++;
            pat = parseAlt();
            if (src[i] === '>') i++;
        } else if (src[i] === '~') {
            i++;
            pat = silence();
        } else {
            const ident = readIdent();
            if (!ident) { i++; return null; }
            pat = pure(ident);
        }
        // Modifiers (greedy).
        while (i < src.length) {
            const c = src[i];
            if (c === '*') {
                i++; const n = readNumber();
                pat = pat.fast(n);
            } else if (c === '/') {
                i++; const n = readNumber();
                pat = pat.slow(n);
            } else if (c === ':') {
                i++; const n = readNumber();
                // sample-index — attach as :n suffix by default.
                pat = pat.withValue(v => `${v}:${n}`);
            } else break;
        }
        return pat;
    }
    // A comma inside a seq/group makes it a polyrhythm — each
    // comma-separated run is its own fastcat, and the runs are stacked
    // in parallel. So "bd*2, hh*4" is stack(fastcat(bd.fast(2)),
    // fastcat(hh.fast(4))): two tracks running at the same tempo.
    function parseSeq(stop = '') {
        const runs = [[]];                // start with one empty run
        while (i < src.length && src[i] !== stop) {
            skipWs();
            if (i >= src.length || src[i] === stop) break;
            if (src[i] === ',') { i++; runs.push([]); continue; }
            const el = parseElem();
            if (el) runs[runs.length - 1].push(el);
        }
        const seqs = runs.map(r => fastcat(...r));
        return (seqs.length === 1) ? seqs[0] : stack(...seqs);
    }
    function parseAlt() {
        // Alternation doesn't mix with commas in Strudel; treat comma as
        // a plain element separator inside <...>.
        const items = [];
        while (i < src.length && src[i] !== '>') {
            skipWs();
            if (i >= src.length || src[i] === '>') break;
            if (src[i] === ',') { i++; continue; }
            const el = parseElem();
            if (el) items.push(el);
        }
        return slowcat(...items);
    }

    return parseSeq('');
}

// ── Top-level user API ────────────────────────────────────────────────
// Accepts either a string (mini notation) or an existing Pattern.
function asPattern(x) {
    if (x instanceof Pattern) return x;
    if (typeof x === 'string') return parseMini(x);
    return pure(x);
}

// s("bd sn") produces a Pattern whose Hap values are { s: "bd" } etc.
function s(arg) {
    return asPattern(arg).withValue(v =>
        (v && typeof v === 'object') ? Object.assign({}, v, { s: v.s || v }) : { s: v });
}
// note("c3 e3") — we keep the token as-is; future synth stage parses it.
function note(arg) {
    return asPattern(arg).withValue(v =>
        (v && typeof v === 'object') ? Object.assign({}, v, { note: v.note || v }) : { note: v });
}

// `cat` in Strudel = slowcat (one per cycle). `seq` / `fastcat` = within-cycle.
const cat = slowcat;
const seq = fastcat;

// Convenience: evaluate user code in a scope with all constructors available.
// Caller passes source. The snippet's final expression becomes the returned
// Pattern (wrapped in an IIFE with the user's code as its body).
function evaluate(userCode) {
    const body = `return (${userCode});`;
    const f = new Function(
        's', 'note', 'stack', 'cat', 'seq', 'fastcat', 'slowcat',
        'silence', 'pure', 'Pattern', body);
    return f(s, note, stack, cat, seq, fastcat, slowcat,
             silence, pure, Pattern);
}

// ── Forward-compat safety net ─────────────────────────────────────────
// Pasted snippets from strudel.cc can reference combinators we haven't
// implemented yet (e.g. `.euclid(3,8)`, `.arp`, `.jux`). Rather than
// crashing the query, each unimplemented method on Pattern.prototype is
// installed as a logging no-op that returns `this` so the chain keeps
// working for the parts that DO exist.
// Conservative list — only methods where "do nothing, return self" is a
// tolerable degradation (not ones that transform values like .range or
// .add, where no-op would silently mis-map the signal).
const _UNIMPL_METHODS = [
    // Structural combinators we'll add later:
    'euclid', 'euclidLegato', 'euclidRot', 'arp', 'arpWith',
    'jux', 'juxBy', 'off', 'ply', 'chunk', 'struct', 'mask', 'when',
    // Audio params we don't DSP yet:
    'vowel', 'shape', 'coarse', 'cut', 'begin', 'end', 'loop',
    'lpq', 'hpq', 'bpq', 'lpenv', 'hpenv', 'phaser', 'tremolo',
    'dry', 'wet',
    // Tempo / time helpers:
    'cps', 'legato', 'swing', 'swingBy', 'shuffle',
];
const _missingLogged = new Set();
for (const m of _UNIMPL_METHODS) {
    if (Pattern.prototype[m]) continue;
    Pattern.prototype[m] = function (...args) {
        if (!_missingLogged.has(m)) {
            _missingLogged.add(m);
            print(`[engine] '.${m}()' not yet implemented — skipped (chain continues)`);
        }
        void args;
        return this;
    };
}

// ── Expose to the host ────────────────────────────────────────────────
globalThis.Pattern  = Pattern;
globalThis.silence  = silence;
globalThis.pure     = pure;
globalThis.stack    = stack;
globalThis.cat      = cat;
globalThis.seq      = seq;
globalThis.fastcat  = fastcat;
globalThis.slowcat  = slowcat;
globalThis.s        = s;
globalThis.note     = note;
globalThis.mini     = parseMini;
globalThis.evaluate = evaluate;

print('[engine] pattern engine v0.1 loaded');
