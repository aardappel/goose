// JSON (RFC 8259; docs/stdlib.md): a writer that appends values to a byte
// builder, a parser that builds a document tree, and a reader that parses
// straight into the caller's own types, the way a serde derive does.

namespace json;

import std;

// --- writing --------------------------------------------------------------------

// s as a JSON string: quoted, with '"', '\' and the control characters
// escaped. Most strings have none of those, and go out in one piece.
fn format_string(out: u8[>..]&, s: u8[:]) {
    out.push('"');
    if plain(s) { out.append(s); } else { format_escaped(out, s); }
    out.push('"');
}

// An object key and its colon.
fn format_key(out: u8[>..]&, key: u8[:]) {
    out.push('"');
    if plain(key) { out.append(key); } else { format_escaped(out, key); }
    out.append("\":");
}

// Whether s holds nothing a JSON string escapes.
fn plain(s: u8[:]) -> bool {
    for c in s {
        if c < 0x20 || c == '"' || c == '\\' { return false; }
    }
    return true;
}

// s with its quotes, backslashes and control characters escaped.
fn format_escaped(out: u8[>..]&, s: u8[:]) {
    var start = 0;
    for c, i in s {
        if c >= 0x20 && c != '"' && c != '\\' { continue; }
        out.append(s[start..i]);
        start = i + 1;
        if c == '"' || c == '\\' { out.push('\\'); out.push(c); }
        else if c == '\n' { out.append("\\n"); }
        else if c == '\r' { out.append("\\r"); }
        else if c == '\t' { out.append("\\t"); }
        else if c == 8 { out.append("\\b"); }
        else if c == 12 { out.append("\\f"); }
        else {
            out.append("\\u00");
            out.push("0123456789abcdef"[c >> 4]);
            out.push("0123456789abcdef"[c & 15]);
        }
    }
    out.append(s[start..]);
}

fn format_bool(out: u8[>..]&, b: bool) {
    out.append(if b { "true" } else { "false" });
}

fn format_int(out: u8[>..]&, v: i64) {
    format(out, v);
}

// The two digits of each number below 100, so that digits come out of a
// division by 100 two at a time.
let DIGIT_PAIRS = "00010203040506070809101112131415161718192021222324252627282930313233343536373839404142434445464748495051525354555657585960616263646566676869707172737475767778798081828384858687888990919293949596979899";

// 10^0 .. 10^22, each exact in binary64.
let POW10: f64[23] = [1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
                      1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22];

// v as a JSON number: the fewest significant digits that read back as v,
// laid out as serde_json's ryu lays them out: 0.5, 1.0, 123.25, 1e-7,
// 1.5e300, 0.30000000000000004. JSON has no NaN or infinity, so those are
// written as null, as serde_json writes them.
fn format_number(out: u8[>..]&, v: f64) {
    if v != v || v - v != 0.0 { out.append("null"); return; }
    if v == 0.0 { out.append(if 1.0 / v < 0.0 { "-0.0" } else { "0.0" }); return; }
    var x = v;
    if x < 0.0 { out.push('-'); x = -x; }
    // A double whose shortest form has at most 15 significant digits, as
    // most have, is x rounded to 15 digits: above the subnormals the
    // 15-digit decimals are further apart than x's neighbours, so at most
    // one of them reads back as x, the nearest. With x * 10^s scaled into
    // [10^14, 10^15) by an exact power of ten, that decimal is the nearest
    // integer n, and it reads back as x exactly when n / 10^s, one
    // correctly rounded division, is x. shortest_digits handles the rest.
    var s = 15;
    var p = x * 1e15;
    while p < 1e14 && s < 22 { s++; p = x * POW10[s]; }
    while p >= 1e15 && s > -22 { s--; p = if s >= 0 { x * POW10[s] } else { x / POW10[-s] }; }
    var n: u64 = 0;
    if p >= 1e14 && p < 1e15 {
        n = (p + 0.5) as! u64;
        if n == 1000000000000000 { n = 100000000000000; s--; }
        let back = if s >= 0 { n / POW10[s] } else { n * POW10[-s] };
        if back != x { n = 0; }
    }
    // x = 0.d1 d2 ... dlen * 10^point
    var digits: u8[17] = [0; 17];
    var len = 15;
    var point = 15 - s;
    if n > 0 {
        put_digits(digits[..15], n);
    } else {
        let l, q = shortest_digits(x, digits);
        len = l;
        point = q;
    }
    while digits[len - 1] == '0' { len--; }
    let d = digits[..len];
    if point >= len && point <= 16 {
        out.append(d);
        for i in point - len { out.push('0'); }
        out.append(".0");
    } else if point > 0 && point <= 16 {
        out.append(d[..point]);
        out.push('.');
        out.append(d[point..]);
    } else if point > -5 && point <= 0 {
        out.append("0.");
        for i in -point { out.push('0'); }
        out.append(d);
    } else {
        out.push(d[0]);
        if len > 1 {
            out.push('.');
            out.append(d[1..]);
        }
        out.push('e');
        format_int(out, point - 1);
    }
}

// The decimal digits of v, below 10^out.len, zero-padded to fill out, two at
// a time.
fn put_digits(out: u8[:], v: u64) {
    var x = v;
    var i = out.len;
    while i >= 2 {
        let r = x % 100;
        x /= 100;
        i -= 2;
        out[i] = DIGIT_PAIRS[2 * r];
        out[i + 1] = DIGIT_PAIRS[2 * r + 1];
    }
    if i == 1 { out[0] = ('0' + x) as! u8; }
}

// The rest of format_number: the digits of the shortest form of x > 0, into
// `out`, with their count and the power of ten they are a fraction of. The
// builtin text of a double has 15 significant digits if those read back,
// which above the subnormals makes it the shortest form once its trailing
// zeros are dropped, and the correctly rounded 17 otherwise. Here 17 digits
// are tried at 16, and a subnormal's at every length below its own, since
// its neighbours lie further apart: at each length the two decimals around
// the digits held, the nearer first, through the exact parser.
fn shortest_digits(x: f64, out: u8[:]) -> i64, i64 {
    var text: u8[..32] = [];
    format(text, x);
    var len = 0;
    var point = 0;
    var frac = false;           // past the point
    var lead = true;            // before the first digit other than 0
    var i = 0;
    while i < text.len && text[i] != 'e' {
        let c = text[i];
        i++;
        if c == '.' { frac = true; continue; }
        if lead && c == '0' {
            if frac { point--; }
            continue;
        }
        lead = false;
        if len < out.len {
            out[len] = c;
            len++;
        }
        if !frac { point++; }
    }
    if i < text.len {
        let e, end = scan_exponent(text, i + 1);
        point += e;
    }
    while len > 1 && out[len - 1] == '0' { len--; }
    let subnormal = x < 2.2250738585072014e-308;
    guard len == 17 || subnormal else { return len, point; }
    var try: u8[17] = [0; 17];
    for k in (if subnormal { 1 } else { len - 1 })..len {
        // The decimal of k digits below the digits held, and the one above.
        copy_into(try[..k], out[..k]);
        let up = out[k] >= '5';
        for side in 2 {
            var n = k;
            var p = point;
            if (side == 0) == up {
                // one more in the last place, carried
                var j = k;
                while j > 0 && try[j - 1] == '9' { j--; }
                if j == 0 {
                    try[0] = '1';
                    n = 1;
                    p++;
                } else {
                    try[j - 1]++;
                    n = j;
                }
            }
            if reads_back(try[..n], p, x) {
                copy_into(out[..n], try[..n]);
                return n, p;
            }
            copy_into(try[..k], out[..k]);
        }
    }
    return len, point;
}

// Whether 0.d1 d2 ... dn * 10^point reads back as x.
fn reads_back(digits: u8[:], point: i64, x: f64) -> bool {
    var text: u8[..40] = [];
    text.append(digits);
    text.push('e');
    format(text, point - digits.len);
    return scan_number_general(text, 0).value == x;
}

// --- scanning -------------------------------------------------------------------

// JSON's white space, all of it at or below ' ', so that the first compare
// settles nearly every byte.
fn is_space(c: u8) -> bool { c <= ' ' && (c == ' ' || c == '\n' || c == '\r' || c == '\t') }

// The first position at or after pos that is not white space.
fn skip_space(text: u8[:], pos: i64) -> i64 {
    var p = pos;
    while p < text.len && is_space(text[p]) { p++; }
    return p;
}

// The string whose opening quote is at text[at - 1], from its first byte:
// the position of its closing quote, and whether it holds an escape. -1 if
// it is unterminated or holds a raw control character. Escapes are passed
// over unchecked: decode_string or valid_escapes checks them.
fn scan_string(text: u8[:], at: i64) -> i64, bool {
    guard at >= 0 else { return -1, false; }
    var pos = at;
    var escaped = false;
    while pos < text.len {
        let c = text[pos];
        if c == '"' { return pos, escaped; }
        if c == '\\' { escaped = true; pos += 2; continue; }
        if c < 0x20 { return -1, false; }
        pos++;
    }
    return -1, false;
}

// Whether every escape in text[from..to], the body of a string, is one of
// \" \\ \/ \b \f \n \r \t \uXXXX.
fn valid_escapes(text: u8[:], from: i64, to: i64) -> bool {
    var pos = from;
    while pos < to {
        if text[pos] != '\\' { pos++; continue; }
        guard pos + 1 < to else { return false; }
        let e = text[pos + 1];
        if e == 'u' {
            guard pos + 6 <= to else { return false; }
            for i in 4 {
                if hex_digit(text[pos + 2 + i]) < 0 { return false; }
            }
            pos += 6;
        } else if e == '"' || e == '\\' || e == '/' || e == 'b' || e == 'f' || e == 'n' || e == 'r' || e == 't' {
            pos += 2;
        } else {
            return false;
        }
    }
    return true;
}

fn hex_digit(c: u8) -> i64 {
    if c >= '0' && c <= '9' { return c - '0'; }
    if c >= 'a' && c <= 'f' { return c - 'a' + 10; }
    if c >= 'A' && c <= 'F' { return c - 'A' + 10; }
    return -1;
}

// The escaped string text[at..end] decoded onto the end of out, and false
// if an escape is malformed.
fn decode_string(text: u8[:], at: i64, end: i64, out: u8[>..]&) -> const u8[:], bool {
    let first = out.len;
    var pos = at;
    while pos < end {
        let c = text[pos];
        pos++;
        if c != '\\' { out.push(c); continue; }
        let e = text[pos];
        pos++;
        if e == '"' || e == '\\' || e == '/' { out.push(e); }
        else if e == 'n' { out.push('\n'); }
        else if e == 't' { out.push('\t'); }
        else if e == 'r' { out.push('\r'); }
        else if e == 'b' { out.push(8); }
        else if e == 'f' { out.push(12); }
        else if e == 'u' {
            guard pos + 4 <= end else { return out[first..], false; }
            var cp = 0;
            for i in 4 {
                let d = hex_digit(text[pos + i]);
                guard d >= 0 else { return out[first..], false; }
                cp = cp * 16 + d;
            }
            pos += 4;
            // A surrogate pair is two escapes that make one code point; a
            // lone half is kept as the three bytes of its own code.
            if cp >= 0xD800 && cp < 0xDC00 && pos + 6 <= end && text[pos] == '\\' && text[pos + 1] == 'u' {
                var lo = 0;
                for i in 4 {
                    let d = hex_digit(text[pos + 2 + i]);
                    guard d >= 0 else { return out[first..], false; }
                    lo = lo * 16 + d;
                }
                if lo >= 0xDC00 && lo < 0xE000 {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    pos += 6;
                }
            }
            push_utf8(out, cp);
        }
        else { return out[first..], false; }
    }
    return out[first..], true;
}

// A JSON number, -?(0|[1-9][0-9]*)(.[0-9]+)?([eE][+-]?[0-9]+)?, and where
// it ends; `is_int` when it has neither a fraction nor an exponent and fits
// an i64.
struct Number { value: f64, int: i64, is_int: bool, end: i64 }

// The number starting at text[at], or one with end == at when there is none.
// A plain decimal whose digits make an integer of at most 2^53, as nearly
// every number is, is read here: that integer and the power of ten that
// divides it are both exact, so the one divide is correctly rounded.
// Anything else is read by scan_number_general.
fn scan_number(text: u8[:], at: i64) -> Number {
    guard at >= 0 else { return Number { value: 0.0, int: 0, is_int: false, end: at }; }
    var pos = at;
    var neg = false;
    if pos < text.len && text[pos] == '-' { neg = true; pos++; }
    let int_start = pos;
    var mant: u64 = 0;
    while pos < text.len && text[pos] >= '0' && text[pos] <= '9' {
        mant = mant * 10 + (text[pos] - '0');
        pos++;
    }
    let int_end = pos;
    var frac_digits = 0;
    if pos < text.len && text[pos] == '.' {
        pos++;
        let frac_start = pos;
        while pos < text.len && text[pos] >= '0' && text[pos] <= '9' {
            mant = mant * 10 + (text[pos] - '0');
            pos++;
        }
        frac_digits = pos - frac_start;
        guard frac_digits > 0 else { return scan_number_general(text, at); }
    }
    let int_digits = int_end - int_start;
    guard int_digits > 0 && (int_digits == 1 || text[int_start] != '0')
          && int_digits + frac_digits <= 19 && mant <= MAX_EXACT
          && !(pos < text.len && (text[pos] == 'e' || text[pos] == 'E')) else {
        return scan_number_general(text, at);
    }
    var v: f64 = mant;
    if frac_digits > 0 { v /= POW10[frac_digits]; }
    if neg { v = -v; }
    let i = if neg { 0 - mant as! i64 } else { mant as! i64 };
    return Number { value: v, int: i, is_int: frac_digits == 0, end: pos };
}

// 2^53: the integers up to it are exact in binary64.
let MAX_EXACT: u64 = 9007199254740992;

// Any number, correctly rounded. Its first 19 significant digits accumulate
// in a u64, and where that integer and the power of ten that scales it are
// both exact, one multiply or divide is correctly rounded. Otherwise the
// value they give in steps is within a few ulps, and round_exact settles it.
fn scan_number_general(text: u8[:], at: i64) -> Number {
    let none = Number { value: 0.0, int: 0, is_int: false, end: at };
    guard at >= 0 else { return none; }
    var pos = at;
    var neg = false;
    if pos < text.len && text[pos] == '-' { neg = true; pos++; }
    let int_start = pos;
    while pos < text.len && text[pos] >= '0' && text[pos] <= '9' { pos++; }
    let int_digits = pos - int_start;
    guard int_digits > 0 && (int_digits == 1 || text[int_start] != '0') else { return none; }
    var frac_digits = 0;
    if pos < text.len && text[pos] == '.' {
        pos++;
        let frac_start = pos;
        while pos < text.len && text[pos] >= '0' && text[pos] <= '9' { pos++; }
        frac_digits = pos - frac_start;
        guard frac_digits > 0 else { return none; }
    }
    var end = pos;
    var exp10 = -frac_digits;
    if pos < text.len && (text[pos] == 'e' || text[pos] == 'E') {
        let e, exp_end = scan_exponent(text, pos + 1);
        guard exp_end > pos + 1 else { return none; }
        exp10 += e;
        end = exp_end;
    }
    // The first 19 significant digits, and how many follow them.
    var mant: u64 = 0;
    var taken = 0;
    var dropped = 0;
    for c in text[int_start..pos] {
        if c == '.' || (taken == 0 && c == '0') { continue; }
        if taken < 19 { mant = mant * 10 + (c - '0'); taken++; } else { dropped++; }
    }
    // A value other than zero is below 10^(digits + exp10) and at least a
    // tenth of that.
    let digits = taken + dropped;
    var v: f64 = mant;
    if digits == 0 || digits + exp10 < -324 {
        v = 0.0;
    } else if digits + exp10 > 310 {
        v = 1.0 / 0.0;
    } else if dropped == 0 && mant <= MAX_EXACT && exp10 >= -22 && exp10 <= 22 {
        v = scale(v, exp10);
    } else {
        v = round_exact(text, int_start, pos, exp10, scale(v, exp10 + dropped));
    }
    if neg { v = -v; }
    // An integer when every digit was taken and it fits: at most 2^63 - 1,
    // or 2^63 below zero, whose negation wraps to itself.
    let is_int = frac_digits == 0 && end == pos && dropped == 0
                 && (mant <= MAX_I64 || (neg && mant == MAX_I64 + 1));
    let i = if neg { (0 - mant) as! i64 } else { mant as! i64 };
    return Number { value: v, int: i, is_int: is_int, end: end };
}

let MAX_I64: u64 = 9223372036854775807;

// The exponent that follows the 'e' at text[at - 1], and where it ends;
// end == at when it has no digits.
fn scan_exponent(text: u8[:], at: i64) -> i64, i64 {
    var pos = at;
    var neg = false;
    if pos < text.len && (text[pos] == '-' || text[pos] == '+') {
        neg = text[pos] == '-';
        pos++;
    }
    let start = pos;
    var e = 0;
    while pos < text.len && text[pos] >= '0' && text[pos] <= '9' {
        if e < 100000 { e = e * 10 + (text[pos] - '0'); }
        pos++;
    }
    if pos == start { return 0, at; }
    if neg { e = -e; }
    return e, pos;
}

// v * 10^e, in steps of an exact power of ten.
fn scale(v: f64, e: i64) -> f64 {
    var x = v;
    var k = e;
    while k > 22 { x *= 1e22; k -= 22; }
    while k < -22 { x /= 1e22; k += 22; }
    if k > 0 { x *= POW10[k]; } else if k < 0 { x /= POW10[-k]; }
    return x;
}

// --- exact rounding -------------------------------------------------------------

// The C library's split of a double into a fraction in [0.5, 1) and a power
// of two, and its inverse.
extern fn frexp(x: f64, e: i32&) -> f64;
extern fn ldexp(x: f64, e: i32) -> f64;

// The double nearest to D * 10^e10, D the decimal digits text[from..to]
// (a point among them is passed over), given an estimate v within a few
// ulps of it. With v = m * 2^k, v steps an ulp at a time until the exact
// value lies within the halfway points to its neighbours, ties going to the
// even m. Each comparison is exact, in big integers.
fn round_exact(text: u8[:], from: i64, to: i64, e10: i64, v: f64) -> f64 {
    var digits: u32[>..] = [];
    for c in text[from..to] {
        if c != '.' { big_mul_add(digits, 10, c - '0'); }
    }
    // m below 2^53, k from -1074, the subnormals' exponent, to 971, the
    // largest finite double's.
    var m: u64 = 0;
    var k = -1074;
    if v - v != 0.0 {
        m = MAX_EXACT - 1;
        k = 971;
    } else if v > 0.0 {
        var e: i32 = 0;
        let f = frexp(v, e);
        m = (f * 9007199254740992.0) as! u64;
        k = e - 53;
        if k < -1074 {
            m >>= -1074 - k;
            k = -1074;
        }
    }
    loop {
        let up = compare_exact(digits, e10, 2 * m + 1, k - 1);
        if up > 0 || (up == 0 && m % 2 == 1) {
            m++;
            if m == MAX_EXACT { m = MAX_EXACT / 2; k++; }
            if k > 971 { return 1.0 / 0.0; }
            continue;
        }
        if m == 0 { break; }
        // Below a power of two the neighbour is half as far.
        let down = if m == MAX_EXACT / 2 && k > -1074 { compare_exact(digits, e10, 4 * m - 1, k - 2) }
                   else { compare_exact(digits, e10, 2 * m - 1, k - 1) };
        if down < 0 || (down == 0 && m % 2 == 1) {
            m--;
            if m < MAX_EXACT / 2 && k > -1074 { m = 2 * m + 1; k--; }
            continue;
        }
        break;
    }
    return ldexp(m, k as! i32);
}

// D * 10^e10 against h * 2^p: -1, 0 or 1 as it is below, equal to or above.
fn compare_exact(digits: u32[:], e10: i64, h: u64, p: i64) -> i64 {
    var a: u32[>..] = [];
    a.append(digits);
    var b: u32[>..] = [];
    b.push(h as! u32);
    if h >> 32 > 0 { b.push((h >> 32) as! u32); }
    if e10 >= 0 { big_mul_pow5(a, e10); } else { big_mul_pow5(b, -e10); }
    if e10 >= p { big_shl(a, e10 - p); } else { big_shl(b, p - e10); }
    return big_cmp(a, b);
}

// Big integers are their base-2^32 limbs, least significant first, with no
// zero limbs on top: zero has none.

// x = x * m + a.
fn big_mul_add(x: u32[>..]&, m: u32, a: u32) {
    var carry: u64 = a;
    for &limb in x {
        let t = limb as u64 * m + carry;
        limb = t as! u32;
        carry = t >> 32;
    }
    if carry > 0 { x.push(carry as! u32); }
}

// x = x * 5^e.
fn big_mul_pow5(x: u32[>..]&, e: i64) {
    var k = e;
    while k >= 13 { big_mul_add(x, 1220703125, 0); k -= 13; }
    var m: u32 = 1;
    for i in k { m *= 5; }
    big_mul_add(x, m, 0);
}

// x = x * 2^e.
fn big_shl(x: u32[>..]&, e: i64) {
    var k = e;
    while k >= 31 { big_mul_add(x, 0x80000000, 0); k -= 31; }
    big_mul_add(x, (1 as u32) << k, 0);
}

fn big_cmp(a: u32[:], b: u32[:]) -> i64 {
    if a.len != b.len { return if a.len < b.len { -1 } else { 1 }; }
    var i = a.len;
    while i > 0 {
        i--;
        if a[i] != b[i] { return if a[i] < b[i] { -1 } else { 1 }; }
    }
    return 0;
}

// --- the document tree ----------------------------------------------------------

// A document is one grow-only pool of fixed-size nodes in text order. A
// node is a link to its next sibling, a link to its first child (null but
// in a container), its key (empty in an array) and its value. Keys and
// strings are views of the text they were parsed from, or of the caller's
// buffer for those that held escapes, so nodes are all one size and nothing
// is copied into them. Links are 4-byte self-relative offsets.
struct Node { next: Node&<u32>?, first: Node&<u32>?, key: const u8[:], val: Value }

enum Value {
    Null,
    Bool { v: bool },
    Int { v: i64 },
    Num { v: f64 },
    Str { s: const u8[:] },
    Arr { count: i64 },
    Obj { count: i64 },
}

// A container the parser is inside of: its node, its last child so far and
// how many it has, and whether it is an object.
struct Open { node: Node?, last: Node?, count: i64, is_obj: bool }

// parse's result for text that stops being JSON at byte pos.
fn parse_error(what: const u8[:], pos: i64) -> Node?, u8[>..] { return null, str("json: ", what, " at byte ", pos); }

// Parses text into doc and returns the root, or null and a message naming
// the byte where the text stops being JSON. Strings with escapes are
// decoded onto `decoded`. The parse is one loop over the values in text
// order, with the innermost container in locals and the ones around it on
// a stack. Each node is linked from its previous sibling, or its parent,
// as it is pushed.
fn parse(text: u8[:], doc: Node[>..]&, decoded: u8[>..]&) -> Node?, u8[>..] {
    var pos = 0;
    var root: Node? = null;
    var parent: Node? = null;       // the innermost container,
    var last: Node? = null;         // its last child so far,
    var count = 0;                  // how many it has,
    var is_obj = false;             // and whether it is an object
    var outer: Open[>..<] = [];     // the containers around it, innermost last
    var member = false;             // whether the value at the cursor has a key

    // The string whose opening quote is at pos: a slice of the text, or of
    // `decoded` when it holds escapes.
    fn string() -> const u8[:] {
        let start = pos + 1;
        let end, escaped = scan_string(text, start);
        if end >= 0 {
            pos = end + 1;
            if !escaped { return text[start..end]; }
            let s, ok = decode_string(text, start, end, decoded);
            if ok { return s; }
        }
        return parse_error("bad string", pos) from parse;
    }

    // An object member's key and its colon.
    fn member_key() -> const u8[:] {
        pos = skip_space(text, pos);
        guard pos < text.len && text[pos] == '"' else { return parse_error("expected a key", pos) from parse; }
        let k = string();
        pos = skip_space(text, pos);
        guard pos < text.len && text[pos] == ':' else { return parse_error("expected ':'", pos) from parse; }
        pos++;
        return k;
    }

    fn word(w: u8[:]) {
        guard pos + w.len <= text.len && text[pos..pos + w.len] == w else { return parse_error("expected a value", pos) from parse; }
        pos += w.len;
    }

    // The value at pos, c its first byte; a container's count is filled in
    // when it closes.
    fn value(c: u8) -> Value {
        if c == '{' { return Value.Obj { count: 0 }; }
        if c == '[' { return Value.Arr { count: 0 }; }
        if c == '"' { return Value.Str { s: string() }; }
        if c == 't' { word("true"); return Value.Bool { v: true }; }
        if c == 'f' { word("false"); return Value.Bool { v: false }; }
        if c == 'n' { word("null"); return Value.Null; }
        let n = scan_number(text, pos);
        guard n.end > pos else {
            let what = if c == '-' || (c >= '0' && c <= '9') { "bad number" } else { "expected a value" };
            return parse_error(what, pos) from parse;
        }
        pos = n.end;
        if n.is_int { return Value.Int { v: n.int }; }
        return Value.Num { v: n.value };
    }

    loop {
        let key = if member { member_key() } else { "" };
        pos = skip_space(text, pos);
        guard pos < text.len else { return parse_error("unexpected end of input", pos); }
        let c = text[pos];
        let val = value(c);
        let node .= doc.push(Node { next: null, first: null, key: key, val: val });
        if last { last.next .= node; } else if parent { parent.first .= node; } else { root .= node; }
        last .= node;
        count++;
        if c == '{' || c == '[' {
            outer.push(Open { node: parent, last: last, count: count, is_obj: is_obj });
            parent .= node;
            last .= null;
            count = 0;
            is_obj = c == '{';
            pos = skip_space(text, pos + 1);
            if pos >= text.len || text[pos] != (if is_obj { '}' } else { ']' }) {
                member = is_obj;
                continue;
            }
        } else {
            pos = skip_space(text, pos);
        }
        // Past a value: each close bracket completes the innermost container,
        // and a comma starts the next value.
        var more = false;
        while outer.len > 0 && pos < text.len {
            let d = text[pos];
            if d == ',' { pos++; more = true; break; }
            guard d == (if is_obj { '}' } else { ']' }) else {
                return parse_error(if is_obj { "expected ',' or '}'" } else { "expected ',' or ']'" }, pos);
            }
            pos++;
            if parent { parent.val = if is_obj { Value.Obj { count: count } } else { Value.Arr { count: count } }; }
            let o = outer[outer.len - 1];
            outer.pop();
            parent .= o.node;
            last .= o.last;
            count = o.count;
            is_obj = o.is_obj;
            pos = skip_space(text, pos);
        }
        if more { member = is_obj; continue; }
        guard outer.len == 0 else { return parse_error(if is_obj { "unclosed object" } else { "unclosed array" }, pos); }
        guard pos == text.len else { return parse_error("trailing input", pos); }
        return root, str("");
    }
}

// The first member of object n named key; null if there is none, or if n
// is no object or null itself, so that lookups chain.
fn member(n: Node?, key: u8[:]) -> Node? {
    guard n else { return null; }
    match n.val {
        Obj => {},
        _ => { return null; },
    }
    var c = n.first;
    while c {
        if c.key == key { return c; }
        c .= c.next;
    }
    return null;
}

// F(child) for each element of an array or member of an object, in order.
fn each<F>(n: Node&) {
    var c = n.first;
    while c {
        F(c);
        c .= c.next;
    }
}

// A number's value as an f64; false for anything else.
fn as_f64(n: Node?) -> f64, bool {
    guard n else { return 0.0, false; }
    match n.val {
        Num x => { return x.v, true; },
        Int i => { return i.v, true; },
        _ => { return 0.0, false; },
    }
}

// An integer's value; false for anything else, a number with a fraction or
// an exponent included.
fn as_i64(n: Node?) -> i64, bool {
    guard n else { return 0, false; }
    match n.val {
        Int i => { return i.v, true; },
        _ => { return 0, false; },
    }
}

fn as_bool(n: Node?) -> bool, bool {
    guard n else { return false, false; }
    match n.val {
        Bool b => { return b.v, true; },
        _ => { return false, false; },
    }
}

// A string's text, a view of the parsed text or of `decoded`; false for
// anything else.
fn as_string(n: Node?) -> const u8[:], bool {
    guard n else { return "", false; }
    match n.val {
        Str t => { return t.s, true; },
        _ => { return "", false; },
    }
}

// --- reading into the caller's types --------------------------------------------

// A cursor over JSON text for parsers that map it straight onto their own
// types: `each_member` and `each_element` walk a container and hand each
// member or element to a block, which reads it with `read_number`,
// `read_string` and the rest, or passes it over with `skip`. A read that
// meets malformed text records what it expected and where, and moves the
// cursor to the end of the text, so that every read after it fails too and
// every loop ends: the parser checks once, with `finish`, when it is done.
struct Reader { text: const u8[:], pos: i64 = 0, error: const u8[:] = "", error_at: i64 = 0 }

fn reader(text: const u8[:]) -> Reader { Reader { text: text, .. } }

fn fail(r: Reader&, what: const u8[:]) {
    if r.error.len == 0 {
        r.error = what;
        r.error_at = r.pos;
    }
    r.pos = r.text.len;
}

// Whether the text was read without an error and holds nothing but white
// space after what was read; records "trailing input" if it holds more.
fn finish(r: Reader&) -> bool {
    if r.error.len > 0 { return false; }
    peek(r);
    if r.pos < r.text.len { fail(r, "trailing input"); return false; }
    return true;
}

// The error as a message: "json: <what> at byte <n>".
fn message(r: Reader&) -> u8[>..] { str("json: ", r.error, " at byte ", r.error_at) }

// The next byte that is not white space, not consumed; 0 at the end.
fn peek(r: Reader&) -> u8 {
    r.pos = skip_space(r.text, r.pos);
    return if r.pos < r.text.len { r.text[r.pos] } else { 0 };
}

fn read_number(r: Reader&) -> f64 {
    let at = skip_space(r.text, r.pos);
    let n = scan_number(r.text, at);
    r.pos = at;
    if n.end == at { fail(r, "expected a number"); return 0.0; }
    r.pos = n.end;
    return n.value;
}

// A number with neither a fraction nor an exponent that fits an i64.
fn read_int(r: Reader&) -> i64 {
    let at = skip_space(r.text, r.pos);
    let n = scan_number(r.text, at);
    r.pos = at;
    if n.end == at || !n.is_int { fail(r, "expected an integer"); return 0; }
    r.pos = n.end;
    return n.int;
}

fn read_bool(r: Reader&) -> bool {
    let c = peek(r);
    if c == 't' { word(r, "true"); return r.error.len == 0; }
    if c == 'f' { word(r, "false"); return false; }
    fail(r, "expected true or false");
    return false;
}

// A string without escapes as a slice of the text; one with escapes is
// decoded onto `scratch` and returned as a slice of that.
fn read_string(r: Reader&, scratch: u8[>..]&) -> const u8[:] {
    if peek(r) != '"' { fail(r, "expected a string"); return ""; }
    let start = r.pos + 1;
    let end, escaped = scan_string(r.text, start);
    if end < 0 { fail(r, "bad string"); return ""; }
    r.pos = end + 1;
    if !escaped { return r.text[start..end]; }
    let s, ok = decode_string(r.text, start, end, scratch);
    if !ok {
        r.pos = start - 1;
        fail(r, "bad escape");
    }
    return s;
}

// F(key) for each member of the object at the cursor; F reads the value.
// Keys are handed over as written, so one with escapes in it never equals
// a plain one.
fn each_member<F>(r: Reader&) {
    if peek(r) != '{' { fail(r, "expected '{'"); return; }
    r.pos++;
    if peek(r) == '}' { r.pos++; return; }
    loop {
        if peek(r) != '"' { fail(r, "expected a key"); return; }
        let start = r.pos + 1;
        let end, escaped = scan_string(r.text, start);
        if end < 0 || (escaped && !valid_escapes(r.text, start, end)) { fail(r, "bad string"); return; }
        r.pos = end + 1;
        if peek(r) != ':' { fail(r, "expected ':'"); return; }
        r.pos++;
        F(r.text[start..end]);
        let c = peek(r);
        if c == ',' { r.pos++; continue; }
        if c == '}' { r.pos++; } else { fail(r, "expected ',' or '}'"); }
        return;
    }
}

// F() for each element of the array at the cursor; F reads the element.
fn each_element<F>(r: Reader&) {
    if peek(r) != '[' { fail(r, "expected '['"); return; }
    r.pos++;
    if peek(r) == ']' { r.pos++; return; }
    loop {
        F();
        let c = peek(r);
        if c == ',' { r.pos++; continue; }
        if c == ']' { r.pos++; } else { fail(r, "expected ',' or ']'"); }
        return;
    }
}

fn word(r: Reader&, w: u8[:]) {
    if r.pos + w.len <= r.text.len && r.text[r.pos..r.pos + w.len] == w { r.pos += w.len; }
    else { fail(r, "expected a value"); }
}

// Passes over the value at the cursor, checking that it is well formed.
// A string, the value most often passed over, is scanned here; anything
// else goes through skip_value's loop.
fn skip(r: Reader&) {
    if peek(r) != '"' { skip_value(r); return; }
    let end, escaped = scan_string(r.text, r.pos + 1);
    if end < 0 || (escaped && !valid_escapes(r.text, r.pos + 1, end)) { fail(r, "bad string"); return; }
    r.pos = end + 1;
}

// skip for any value: one loop over the values in it, with the containers
// around the cursor on a stack.
fn skip_value(r: Reader&) {
    var open: bool[>..<] = [];      // the containers around the cursor, true for an object
    var member = false;             // whether the value at the cursor has a key
    loop {
        if member {
            if peek(r) != '"' { fail(r, "expected a key"); return; }
            let end, escaped = scan_string(r.text, r.pos + 1);
            if end < 0 || (escaped && !valid_escapes(r.text, r.pos + 1, end)) { fail(r, "bad string"); return; }
            r.pos = end + 1;
            if peek(r) != ':' { fail(r, "expected ':'"); return; }
            r.pos++;
        }
        let c = peek(r);
        if c == '{' || c == '[' {
            r.pos++;
            open.push(c == '{');
            if peek(r) != (if c == '{' { '}' } else { ']' }) {
                member = c == '{';
                continue;
            }
            r.pos++;
            open.pop();
        } else if c == '"' {
            let end, escaped = scan_string(r.text, r.pos + 1);
            if end < 0 || (escaped && !valid_escapes(r.text, r.pos + 1, end)) { fail(r, "bad string"); return; }
            r.pos = end + 1;
        } else if c == 't' {
            word(r, "true");
        } else if c == 'f' {
            word(r, "false");
        } else if c == 'n' {
            word(r, "null");
        } else if c == '-' || (c >= '0' && c <= '9') {
            read_number(r);
        } else {
            fail(r, "expected a value");
            return;
        }
        // Past a value: each close bracket ends the innermost container,
        // and a comma starts the next value.
        member = false;
        loop {
            if open.len == 0 { return; }
            let in_obj = open[open.len - 1];
            let d = peek(r);
            if d == ',' {
                r.pos++;
                member = in_obj;
                break;
            }
            if d != (if in_obj { '}' } else { ']' }) {
                fail(r, if in_obj { "expected ',' or '}'" } else { "expected ',' or ']'" });
                return;
            }
            r.pos++;
            open.pop();
        }
    }
}
