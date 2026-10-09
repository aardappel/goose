// Base64 (RFC 4648 §4: the standard alphabet, '=' padding; docs/stdlib.md).
//
// The alphabet is computed rather than looked up in a table, every access in
// the loops is proven in bounds, and encode and decode are `simd fn`s (spec
// §7.12): the C compiler can then vectorize them, which no x86 vector unit
// does well for table lookups, and where clang builds for x86-64 they run as
// AVX2 or AVX-512 code on a CPU that has it.

namespace base64;

import std;

// What `value` gives for a byte outside the alphabet.
let INVALID: u8 = 255;

// The character of a 6-bit value (RFC 4648, table 1): an offset added to
// it, chosen by range. Unsigned arithmetic wraps, so the last three, which
// are negative, are written modulo 256.
fn symbol(b: u8) -> u8 {
    var d: u8 = 'A';
    if b >= 26 { d = 'a' - 26; }
    if b >= 52 { d = 252; }             // '0' - 52
    if b == 62 { d = 237; }             // '+' - 62
    if b == 63 { d = 240; }             // '/' - 63
    return b + d;
}

// The 6-bit value of a character, INVALID for anything else. A range test
// is one wrapping subtraction and compare.
fn value(c: u8) -> u8 {
    var v = INVALID;
    if c - 'A' < 26 { v = c - 'A'; }
    if c - 'a' < 26 { v = c - 'a' + 26; }
    if c - '0' < 10 { v = c - '0' + 52; }
    if c == '+' { v = 62; }
    if c == '/' { v = 63; }
    return v;
}

// Appends the text of `src` to `out`: four characters for every three bytes,
// the last group padded with '='.
simd fn encode(out: u8[>..]&, src: const u8[:]) {
    let start = out.len;
    out.resize(start + (src.len + 2) / 3 * 4, 0);
    let dst = out[start..];
    var s = src;
    var d = dst;
    // d always has room for the rest of s; testing it as well lets the
    // compiler see that every store is in bounds.
    while s.len >= 3 && d.len >= 4 {
        d[0] = symbol(s[0] >> 2);
        d[1] = symbol((s[0] & 3) << 4 | s[1] >> 4);
        d[2] = symbol((s[1] & 15) << 2 | s[2] >> 6);
        d[3] = symbol(s[2] & 63);
        s = s[3..];
        d = d[4..];
    }
    if s.len > 0 && d.len >= 4 {
        let s1: u8 = if s.len == 2 { s[1] } else { 0 };
        d[0] = symbol(s[0] >> 2);
        d[1] = symbol((s[0] & 3) << 4 | s1 >> 4);
        d[2] = if s.len == 2 { symbol((s1 & 15) << 2) } else { '=' };
        d[3] = '=';
    }
}

// Appends the bytes `src` encodes to `out`, and returns whether `src` is
// valid base64: whole groups of four alphabet characters, the last of which
// may end in one or two '='. Invalid input leaves `out` as it was.
simd fn decode(out: u8[>..]&, src: const u8[:]) -> bool {
    if src.len % 4 != 0 { return false; }
    var padding = 0;
    if src.len > 0 && src[src.len - 1] == '=' {
        padding = if src[src.len - 2] == '=' { 2 } else { 1 };
    }
    let body = src[..src.len - (if padding > 0 { 4 } else { 0 })];
    let start = out.len;
    out.resize(start + body.len / 4 * 3 + (if padding > 0 { 3 - padding } else { 0 }), 0);
    let dst = out[start..];
    var s = body;
    var o = dst;
    // Validity is accumulated rather than tested per group, which keeps the
    // loop free of exits: values fit in six bits, so only an INVALID among
    // them ORs to INVALID.
    var seen: u8 = 0;
    while s.len >= 4 && o.len >= 3 {
        let a = value(s[0]);
        let b = value(s[1]);
        let c = value(s[2]);
        let d = value(s[3]);
        seen |= a | b | c | d;
        o[0] = a << 2 | b >> 4;
        o[1] = b << 4 | c >> 2;
        o[2] = c << 6 | d;
        s = s[4..];
        o = o[3..];
    }
    var valid = seen != INVALID;
    if valid && padding > 0 {
        let tail = src[body.len..];
        let a = value(tail[0]);
        let b = value(tail[1]);
        let c = if padding == 1 { value(tail[2]) } else { 0 };
        if (a | b | c) == INVALID {
            valid = false;
        } else if o.len >= 1 {
            o[0] = a << 2 | b >> 4;
            if padding == 1 && o.len >= 2 { o[1] = b << 4 | c >> 2; }
        }
    }
    if !valid { out.resize(start); }
    return valid;
}
