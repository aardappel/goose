// runtime-debug: stdlib/utf8.goose against a plain decoder written here to
// Unicode table 3-7: every string of one to three bytes, random strings
// over the bytes at the edges of every class, and valid text damaged in one
// byte, each function at every position; then the test vectors of Go's
// unicode/utf8, utf8proc's iterate test and Kuhn's UTF-8 stress test, and
// the rune indexing by hand.
import std;
import utf8;

// Table 3-7: the first byte chooses the length and the range of the second
// byte; every later byte is 80 to BF. Invalid: ERROR and 1, as utf8::decode.
fn plain_decode(s: u8[:]) -> i64, i64 {
    if s.len == 0 { return utf8::ERROR, 0; }
    let b0 = s[0] as i64;
    if b0 < 0x80 { return b0, 1; }
    var n = 0;
    var lo = 0x80;
    var hi = 0xBF;
    var cp = 0;
    if b0 >= 0xC2 && b0 <= 0xDF {
        n = 2;
        cp = b0 & 0x1F;
    } else if b0 >= 0xE0 && b0 <= 0xEF {
        n = 3;
        cp = b0 & 0x0F;
        if b0 == 0xE0 { lo = 0xA0; }
        if b0 == 0xED { hi = 0x9F; }
    } else if b0 >= 0xF0 && b0 <= 0xF4 {
        n = 4;
        cp = b0 & 0x07;
        if b0 == 0xF0 { lo = 0x90; }
        if b0 == 0xF4 { hi = 0x8F; }
    } else {
        return utf8::ERROR, 1;
    }
    if s.len < n { return utf8::ERROR, 1; }
    var i = 1;
    while i < n {
        let b = s[i] as i64;
        let l = if i == 1 { lo } else { 0x80 };
        let h = if i == 1 { hi } else { 0xBF };
        if b < l || b > h { return utf8::ERROR, 1; }
        cp = (cp << 6) | (b & 0x3F);
        i += 1;
    }
    return cp, n;
}

fn plain_len(s: u8[:]) -> i64 {
    var n = 0;
    var i = 0;
    while i < s.len {
        let cp, w = plain_decode(s[i..]);
        i += w;
        n += 1;
    }
    return n;
}

// A string is valid when no rune is an error; an encoded U+FFFD has width 3.
fn plain_valid(s: u8[:]) -> bool {
    var i = 0;
    while i < s.len {
        let cp, w = plain_decode(s[i..]);
        if cp == utf8::ERROR && w == 1 { return false; }
        i += w;
    }
    return true;
}

fn plain_offset(s: u8[:], n: i64) -> i64 {
    var i = 0;
    var k = 0;
    while k < n && i < s.len {
        let cp, w = plain_decode(s[i..]);
        i += w;
        k += 1;
    }
    return i;
}

var checks = 0;
var failures = 0;

fn fail(what: u8[:], s: u8[:]) {
    failures += 1;
    if failures > 20 { return; }
    var line: u8[>..] = [];
    format(line, "MISMATCH ", what, " on ");
    hex(line, s);
    print(line);
}

fn hex(out: u8[>..]&, s: u8[:]) {
    for b, i in s {
        if i > 0 { out.push(' '); }
        format_int(out, b, 16, 2, '0');
    }
}

// Every function against the plain decoder on s.
fn check(s: u8[:]) {
    checks += 1;
    let cp, w = utf8::decode(s);
    let pcp, pw = plain_decode(s);
    if cp != pcp || w != pw { fail("decode", s); }
    if utf8::len(s) != plain_len(s) { fail("len", s); }
    if utf8::valid(s) != plain_valid(s) { fail("valid", s); }
}

// check at every position and prefix, and the indexing functions.
fn check_all(s: u8[:]) {
    for i in s.len + 1 {
        check(s[i..]);
        // The last rune of a prefix is the last the forward decoder finds.
        let p = s[..i];
        var lcp = utf8::ERROR;
        var lw = 0;
        var j = 0;
        while j < p.len {
            let cp, w = plain_decode(p[j..]);
            lcp = cp;
            lw = w;
            j += w;
        }
        let cp, w = utf8::decode_last(p);
        if cp != lcp || w != lw { fail("decode_last", p); }
    }
    let n = plain_len(s);
    for k in n + 2 {
        if utf8::offset(s, k) != plain_offset(s, k) { fail("offset", s); }
        if utf8::take(s, k) != s[..plain_offset(s, k)] { fail("take", s); }
        for j in k + 1 {
            if utf8::slice(s, j, k) != s[plain_offset(s, j)..plain_offset(s, k)] { fail("slice", s); }
        }
    }
    // next and each walk the same runes as the forward decoder.
    var pos = 0;
    var i = 0;
    var count = 0;
    while true {
        let cp, ok = utf8::next(s, pos);
        if !ok { break; }
        let pcp, pw = plain_decode(s[i..]);
        if cp != pcp || pos != i + pw { fail("next", s); }
        i += pw;
        count += 1;
    }
    if count != n || pos != s.len { fail("next count", s); }
    i = 0;
    count = 0;
    utf8::each(s) { cp, start, width =>
        let pcp, pw = plain_decode(s[i..]);
        if cp != pcp || start != i || width != pw { fail("each", s); }
        i += pw;
        count += 1;
    };
    if count != n { fail("each count", s); }
}

// The bytes at the edges of every class of the automaton.
let EDGES = "\x00\x41\x7F\x80\x8F\x90\x9F\xA0\xBF\xC0\xC1\xC2\xDF\xE0\xE1\xEC\xED\xEE\xEF\xF0\xF1\xF3\xF4\xF5\xFF";

fn against_plain() {
    var s: u8[>..] = [];
    for a in 256 {
        s.clear();
        s.push(a as u8);
        check(s);
        check_all(s);
    }
    for a in 256 {
        for b in 256 {
            s.clear();
            s.push(a as u8);
            s.push(b as u8);
            check(s);
        }
    }
    for a in 256 {
        for b in 256 {
            for c in 256 {
                s.clear();
                s.push(a as u8);
                s.push(b as u8);
                s.push(c as u8);
                check(s);
            }
        }
    }
    print(checks, " strings of one to three bytes decode as the plain decoder says");

    var r = rng { 2024 };
    checks = 0;
    var rounds = 0;
    for round in 20000 {
        let n = 2 + r.rand_int(11);
        s.clear();
        for i in n { s.push(EDGES[r.rand_int(EDGES.len)]); }
        check_all(s);
        rounds += 1;
    }
    print(rounds, " random strings over the class edges, ", checks, " checks at every position");

    // Random runes of every width, encoded, decode to what went in; with one
    // byte changed they still decode as the plain decoder says.
    checks = 0;
    var runes: i64[>..] = [];
    var damaged = 0;
    for round in 5000 {
        s.clear();
        runes.clear();
        let n = r.rand_int(9);
        for i in n {
            var cp = 0;
            let kind = r.rand_int(4);
            if kind == 0 { cp = r.rand_int(0x80); }
            if kind == 1 { cp = 0x80 + r.rand_int(0x800 - 0x80); }
            if kind == 2 { cp = 0x800 + r.rand_int(0x10000 - 0x800); }
            if kind == 3 { cp = 0x10000 + r.rand_int(0x110000 - 0x10000); }
            if cp >= 0xD800 && cp <= 0xDFFF { cp = 0xFFFD; }
            runes.push(cp);
            utf8::encode(s, cp);
        }
        if !utf8::valid(s) || utf8::len(s) != n { fail("valid text", s); }
        var k = 0;
        utf8::each(s) { cp, start, width =>
            if cp != runes[k] || width != utf8::width(cp) { fail("valid text rune", s); }
            k += 1;
        };
        check_all(s);
        if s.len > 0 {
            s[r.rand_int(s.len)] = EDGES[r.rand_int(EDGES.len)];
            check_all(s);
            damaged += 1;
        }
    }
    print(damaged, " valid texts damaged in one byte, ", checks, " checks");
}

// One line per case: the bytes, then len, valid and every rune.
fn show(cases: const u8[:][:]) {
    var line: u8[>..] = [];
    for c in cases {
        line.clear();
        hex(line, c);
        format(line, " | ", utf8::len(c), " ", utf8::valid(c), " |");
        utf8::each(c) { cp, start, width =>
            format(line, " U+");
            format_int(line, cp, 16, 4, '0');
            format(line, "/", width);
        };
        print(line);
    }
}

fn vectors() {
    // Go's utf8map and surrogateMap: the first and last code point of each
    // width and some in between, encoded and decoded.
    let points = [0x0000, 0x0001, 0x007E, 0x007F, 0x0080, 0x0081, 0x00BF, 0x00C0, 0x00C1, 0x00C8,
                  0x00D0, 0x00E0, 0x00F0, 0x00F8, 0x00FF, 0x0100, 0x07FF, 0x0800, 0x0801, 0xD7FF,
                  0xD800, 0xDFFF, 0xE000, 0xFFFD, 0xFFFE, 0xFFFF, 0x10000, 0x10001, 0x10FFFF,
                  0x110000, -1];
    var line: u8[>..] = [];
    var bytes: u8[>..] = [];
    for cp in points {
        bytes.clear();
        utf8::encode(bytes, cp);
        let back, w = utf8::decode(bytes);
        let last, lw = utf8::decode_last(bytes);
        line.clear();
        format(line, "U+");
        format_int(line, cp, 16, 4, '0');
        format(line, " valid_rune ", utf8::valid_rune(cp), " width ", utf8::width(cp), " -> ");
        hex(line, bytes);
        format(line, " -> U+");
        format_int(line, back, 16, 4, '0');
        format(line, "/", w, " last U+");
        format_int(line, last, 16, 4, '0');
        format(line, "/", lw);
        print(line);
    }

    print("Go: invalid sequences, RuneCountTests and ValidTests");
    show(["\xED\xA0\x80", "\xED\xBF\xBF", "\xF4\x90\x80\x80", "\xC0\x80", "\xC1\xBF", "\xE0\x80\x80",
          "\xE0\x9F\xBF", "\xF0\x80\x80\x80", "\xF0\x8F\xBF\xBF", "\x80", "\xBF", "\xF5\x80\x80\x80",
          "\xF8\x80\x80\x80\x80", "\xFE", "\xFF", "\xE2\x82", "\xF0\x9F\x98", "\xC2", "\xE2\x82A",
          "abcd", "☺☻☹", "1,2,3,4", "\xE2\x00", "\xE2\x80", "a\xE2\x80", "", "a", "abc", "Ж", "ЖЖ",
          "брэд-ЛГТМ", "aa\xE2", "66\xE2\x82\xAC", "\xF4\x8F\xBF\xBF", "\xF4\x90\x80\x80",
          "\xED\x9F\xBF", "\xEE\x80\x80"]);

    print("utf8proc iterate: ASCII, each width, overlongs, surrogates, 5 and 6 bytes, truncation");
    show(["\x00", "A", "\x7F", "\xC2\x80", "\xDF\xBF", "\xE0\xA0\x80", "\xEF\xBF\xBF", "\xF0\x90\x80\x80",
          "\xF4\x8F\xBF\xBF", "\xC0\xAF", "\xE0\x80\xAF", "\xF0\x80\x80\xAF", "\xED\xA0\x80\xED\xB0\x80",
          "\xF8\x88\x80\x80\x80", "\xFC\x84\x80\x80\x80\x80", "\xC2", "\xE0\xA0", "\xF0\x90\x80",
          "\xC2\x41", "\xE0\xA0\x41", "\xF0\x90\x80\x41", "\x80\x80", "\xEF\xBF\xBE"]);

    print("Kuhn: first and last of each length, boundaries, lonely starts, overlongs, surrogates");
    show(["\xC2\x80", "\xE0\xA0\x80", "\xF0\x90\x80\x80", "\xF8\x88\x80\x80\x80", "\xFC\x84\x80\x80\x80\x80",
          "\x7F", "\xDF\xBF", "\xEF\xBF\xBF", "\xF7\xBF\xBF\xBF", "\xFB\xBF\xBF\xBF\xBF",
          "\xFD\xBF\xBF\xBF\xBF\xBF", "\xED\x9F\xBF", "\xEE\x80\x80", "\xEF\xBF\xBD", "\xF4\x8F\xBF\xBF",
          "\xF4\x90\x80\x80", "\xC0 \xC1 \xDF ", "\xE0 \xEF ", "\xF0 \xF7 ", "\xF8 \xFB ", "\xFC \xFD ",
          "\xFE", "\xFF", "\xFE\xFE\xFF\xFF", "\xC0\xAF", "\xE0\x80\xAF", "\xF0\x80\x80\xAF",
          "\xF8\x80\x80\x80\xAF", "\xFC\x80\x80\x80\x80\xAF", "\xC1\xBF", "\xE0\x9F\xBF", "\xF0\x8F\xBF\xBF",
          "\xC0\x80", "\xE0\x80\x80", "\xF0\x80\x80\x80", "\xED\xA0\x80", "\xED\xAD\xBF", "\xED\xAE\x80",
          "\xED\xAF\xBF", "\xED\xB0\x80", "\xED\xBE\x80", "\xED\xBF\xBF", "\xED\xA0\x80\xED\xB0\x80",
          "\xED\xAF\xBF\xED\xBF\xBF", "\xEF\xBF\xBE", "\xEF\xBF\xBF"]);
}

// The example from the feature request, and the indexing functions by hand.
fn indexing() {
    let s = "你好世界abc";
    print(utf8::slice(s, 0, 2), " ", utf8::slice(s, 2, 4), " ", utf8::slice(s, 4, 7), " ", utf8::len(s));
    print(utf8::take(s, 3), " [", utf8::take(s, 0), "] ", utf8::take(s, 100), " [", utf8::slice(s, 3, 3), "]");
    print(utf8::slice(s, 6, 100), " ", utf8::slice(s, 7, 7).len, " ", utf8::slice(s, 100, 200).len);
    var line: u8[>..] = [];
    for n in utf8::len(s) + 2 { format(line, utf8::offset(s, n), " "); }
    print(line);
    let e = "a😀b🇭🇺c";                                          // 4-byte runes and a flag pair
    print(utf8::len(e), " ", utf8::slice(e, 1, 2), " ", utf8::slice(e, 3, 5), " ", utf8::take(e, 2));
    let d = "x\xE2\x82y€";                                       // a truncated € before a whole one
    print(utf8::len(d), " ", utf8::valid(d), " ", utf8::slice(d, 4, 5), " ", utf8::take(d, 1), " ", utf8::slice(d, 1, 3).len);
    var pos = 0;
    line.clear();
    while true {
        let cp, ok = utf8::next(s, pos);
        if !ok { break; }
        format(line, cp, "@", pos, " ");
    }
    print(line);
    // encode: what valid_rune rejects is written as U+FFFD.
    var out: u8[>..] = [];
    utf8::encode(out, 'g');
    utf8::encode(out, 0xD800);
    utf8::encode(out, -5);
    utf8::encode(out, 0x110000);
    utf8::encode(out, 0x10FFFF);
    line.clear();
    hex(line, out);
    print(line, " ", utf8::len(out), " ", utf8::valid(out));
}

fn main() {
    against_plain();
    if failures > 0 { print(failures, " FAILURES"); }
    vectors();
    indexing();
}
