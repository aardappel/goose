// each_utf8 on malformed input: one 0xFFFD per maximal subpart, as Unicode
// §3.9 and the WHATWG decoder count them; push_utf8 out of range; and every
// scalar value written and read back.
import std;

fn show(s: u8[:]) {
    var line: u8[>..] = [];
    each_utf8(s) {
        if line.len > 0 { line.push(' '); }
        format_int(line, it, 16, 0, ' ');
    };
    print(line);
}

fn main() {
    show([0xC3, 'A']);                    // a lead byte whose continuation is missing
    show([0xC3]);                         // cut short at the end
    show([0xE2, 0x82]);                   // two of three bytes, at the end
    show([0xE2, 0x82, 'x']);              // two of three bytes, then ASCII
    show([0xC0, 0x80]);                   // overlong NUL
    show([0xC1, 0xBF]);                   // overlong
    show([0xE0, 0x80, 0xAF]);             // overlong three-byte
    show([0xF0, 0x80, 0x80, 0xAF]);       // overlong four-byte
    show([0xED, 0xA0, 0x80]);             // a surrogate
    show([0xED, 0x9F, 0xBF]);             // U+D7FF, just below the surrogates
    show([0xF4, 0x90, 0x80, 0x80]);       // past U+10FFFF
    show([0xF4, 0x8F, 0xBF, 0xBF]);       // U+10FFFF
    show([0xF5, 0x80, 0x80, 0x80]);       // a byte that never leads
    show([0xFF, 0xFE]);
    show([0x80, 0xBF, 'a']);              // continuation bytes alone
    show([0xF0, 0x9F, 0x98, 'a']);        // three of four bytes, then ASCII
    show([0xE0, 0xA0, 0x80, 0xEF, 0xBF, 0xBF]);   // U+0800 and U+FFFF
    show([0xF0, 0x90, 0x80, 0x80]);       // U+10000
    show("héllo€");

    var out: u8[>..] = [];
    push_utf8(out, -1);
    push_utf8(out, 0x110000);
    push_utf8(out, 0x7FFFFFFFFFFFFFFF);
    show(out);                            // fffd fffd fffd

    // Every code point that is not a surrogate reads back as itself, from
    // the number of bytes its range takes.
    var bad = 0;
    var bytes = 0;
    var cp = 0;
    while cp <= 0x10FFFF {
        if cp == 0xD800 { cp = 0xE000; }
        var b: u8[>..] = [];
        push_utf8(b, cp);
        bytes += b.len;
        var n = 0;
        var got = -1;
        each_utf8(b) { n += 1; got = it; };
        if n != 1 || got != cp { bad += 1; }
        cp += 1;
    }
    print(bad, " ", bytes);
}
