// UTF-8 (docs/stdlib.md): decoding, validation and indexing by code point
// ("rune") over u8 arrays.
//
// The decoder is Björn Höhrmann's automaton (bjoern.hoehrmann.de/utf-8/
// decoder/dfa/), which accepts exactly the well-formed sequences of Unicode
// table 3-7: no overlong forms, surrogates, values above U+10FFFF, or the
// bytes C0, C1 and F5 to FF.
//
// Invalid input decodes as in Go's unicode/utf8: the first byte of a
// malformed sequence is one rune, ERROR with a width of 1, and decoding
// resumes at the next byte. So every function is defined on any bytes, and
// a slice never splits a well-formed sequence.

namespace utf8;

import std;

// The replacement character: what an invalid sequence decodes to.
let ERROR: i64 = 0xFFFD;

// The largest code point.
let MAX: i64 = 0x10FFFF;

// The automaton's states are rows of TRANSITION: ACCEPT between sequences,
// REJECT once a byte cannot continue one.
let ACCEPT: i64 = 0;
let REJECT: i64 = 12;

// The class of each byte: 0 ASCII, 1, 7 and 9 continuation bytes by range,
// 2 to 6, 10 and 11 leading bytes by what may follow them, 8 a byte that
// starts nothing.
const CLASS: u8[256] = [
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
    8, 8, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
    10, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 4, 3, 3, 11, 6, 6, 6, 5, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8,
];

// The next state, indexed by state plus class.
const TRANSITION: u8[108] = [
    0, 12, 24, 36, 60, 96, 84, 12, 12, 12, 48, 72, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
    12, 0, 12, 12, 12, 12, 12, 0, 12, 0, 12, 12, 12, 24, 12, 12, 12, 12, 12, 24, 12, 24, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 24, 12, 12, 12, 12, 12, 24, 12, 12, 12, 12, 12, 12, 12, 24, 12, 12,
    12, 12, 12, 12, 12, 12, 12, 36, 12, 36, 12, 12, 12, 36, 12, 12, 12, 12, 12, 36, 12, 36, 12, 12,
    12, 36, 12, 12, 12, 12, 12, 12, 12, 12, 12, 12,
];

// The rune at byte i of s and its width; i must be in range.
fn at(s: u8[:], i: i64) -> i64, i64 {
    let b = s[i];
    if b < 0x80 { return b, 1; }
    let t = CLASS[b] as i64;
    var cp = (0xFF >> t) & (b as i64);
    var state = TRANSITION[t] as i64;
    var j = i + 1;
    while state != ACCEPT && state != REJECT && j < s.len {
        let c = s[j];
        cp = ((c as i64) & 0x3F) | (cp << 6);
        state = TRANSITION[state + CLASS[c]];
        j += 1;
    }
    if state != ACCEPT { return ERROR, 1; }
    return cp, j - i;
}

// The first rune of s and its width; ERROR, 0 if s is empty.
fn decode(s: u8[:]) -> i64, i64 {
    if s.len == 0 { return ERROR, 0; }
    return at(s, 0);
}

// The last rune of s and its width; ERROR, 0 if s is empty.
fn decode_last(s: u8[:]) -> i64, i64 {
    if s.len == 0 { return ERROR, 0; }
    let end = s.len;
    if s[end - 1] < 0x80 { return s[end - 1], 1; }
    // The sequence starting at the last byte that could start one must end
    // at end.
    var start = end - 1;
    let lim = max(end - 4, 0);
    while start > lim && (s[start] & 0xC0) == 0x80 { start -= 1; }
    let cp, w = at(s, start);
    if start + w != end { return ERROR, 1; }
    return cp, w;
}

// The rune at byte pos of s and true, with pos moved past it; false at the
// end. pos must not be negative.
fn next(s: u8[:], pos: i64&) -> i64, bool {
    if pos < 0 { abort("utf8::next: a negative position"); }
    if pos >= s.len { return ERROR, false; }
    let cp, w = at(s, pos);
    pos += w;
    return cp, true;
}

// F(cp, start, width) for each rune of s: its code point, byte offset and
// width.
fn each<F>(s: u8[:]) {
    var i = 0;
    while i < s.len {
        let cp, w = at(s, i);
        F(cp, i, w);
        i += w;
    }
}

// The width of the rune at byte i of s; i must be in range.
fn width_at(s: u8[:], i: i64) -> i64 {
    let b = s[i];
    if b < 0x80 { return 1; }
    var state = TRANSITION[CLASS[b] as i64] as i64;
    var j = i + 1;
    while state != ACCEPT && state != REJECT && j < s.len {
        state = TRANSITION[state + CLASS[s[j]]];
        j += 1;
    }
    return if state == ACCEPT { j - i } else { 1 };
}

// The number of runes in s.
fn len(s: u8[:]) -> i64 {
    var n = 0;
    var i = 0;
    while i < s.len {
        i += width_at(s, i);
        n += 1;
    }
    return n;
}

// The byte offset of rune n of s: s.len when s has no more than n runes. n
// must not be negative.
fn offset(s: u8[:], n: i64) -> i64 {
    if n < 0 { abort("utf8::offset: a negative rune index"); }
    var i = 0;
    var k = 0;
    while k < n && i < s.len {
        i += width_at(s, i);
        k += 1;
    }
    return i;
}

// Runes start up to end of s, as a slice into s; an end past the last rune
// is s.len. start must not exceed end.
fn slice(s: u8[:], start: i64, end: i64) -> u8[:] {
    if start > end { abort("utf8::slice: start exceeds end"); }
    let a = offset(s, start);
    let b = a + offset(s[a..], end - start);
    return s[a..b];
}

// The first n runes of s, as a slice into s.
fn take(s: u8[:], n: i64) -> u8[:] {
    return s[..offset(s, n)];
}

// Whether s is well-formed UTF-8. REJECT absorbs, so the loop needs no test.
fn valid(s: u8[:]) -> bool {
    var state = ACCEPT;
    for b in s {
        state = TRANSITION[state + CLASS[b]];
    }
    return state == ACCEPT;
}

// Whether UTF-8 can encode cp: 0 to MAX and not a surrogate.
fn valid_rune(cp: i64) -> bool {
    return cp >= 0 && cp <= MAX && (cp < 0xD800 || cp > 0xDFFF);
}

// The bytes that encode cp: 1 to 4, or 0 if valid_rune is false.
fn width(cp: i64) -> i64 {
    return if !valid_rune(cp) { 0 }
           else if cp < 0x80 { 1 }
           else if cp < 0x800 { 2 }
           else if cp < 0x10000 { 3 }
           else { 4 };
}

// Appends the bytes of cp to out, ERROR's if valid_rune rejects cp.
fn encode(out: u8[>..]&, cp: i64) {
    var c = cp;
    if !valid_rune(c) { c = ERROR; }
    if c < 0x80 {
        out.push(c as u8);
    } else if c < 0x800 {
        out.push((0xC0 | (c >> 6)) as u8);
        out.push((0x80 | (c & 0x3F)) as u8);
    } else if c < 0x10000 {
        out.push((0xE0 | (c >> 12)) as u8);
        out.push((0x80 | ((c >> 6) & 0x3F)) as u8);
        out.push((0x80 | (c & 0x3F)) as u8);
    } else {
        out.push((0xF0 | (c >> 18)) as u8);
        out.push((0x80 | ((c >> 12) & 0x3F)) as u8);
        out.push((0x80 | ((c >> 6) & 0x3F)) as u8);
        out.push((0x80 | (c & 0x3F)) as u8);
    }
}
