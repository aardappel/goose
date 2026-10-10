// runtime-debug: stdlib/base64.goose against RFC 4648's test vectors and a
// plain table-driven codec written here: round trips at every length to 200
// and some long ones (each length mod 3, and the vector loops' bodies),
// every byte at every position of valid texts (every invalid character, '='
// anywhere), padding cases, appending after what the builder holds, and
// invalid input leaving the builder as it was. The runner also builds this
// file with clang at each instruction-set level of the simd functions.
import std;
import base64;

let ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// RFC 4648 §4, bit by bit.
fn plain_encode(out: u8[>..]&, s: u8[:]) {
    var acc: u32 = 0;
    var bits = 0;
    for c in s {
        acc = acc << 8 | c;
        bits += 8;
        while bits >= 6 {
            bits -= 6;
            out.push(ALPHABET[(acc >> bits) & 63]);
        }
    }
    if bits > 0 { out.push(ALPHABET[(acc << (6 - bits)) & 63]); }
    while out.len % 4 != 0 { out.push('='); }
}

fn plain_decode(out: u8[>..]&, s: u8[:]) -> bool {
    guard s.len % 4 == 0 else { return false; }
    var padding = 0;
    if s.len > 0 && s[s.len - 1] == '=' { padding = if s[s.len - 2] == '=' { 2 } else { 1 }; }
    var bytes: u8[>..] = [];
    var acc: u32 = 0;
    var bits = 0;
    for c in s[..s.len - padding] {
        let v = position(ALPHABET, c);
        guard v >= 0 else { return false; }
        acc = acc << 6 | v as u32;
        bits += 6;
        if bits >= 8 {
            bits -= 8;
            bytes.push((acc >> bits) as! u8);
        }
    }
    out.append(bytes);
    return true;
}

var checks = 0;

fn check_text(text: u8[:], prefix: u8[:]) {
    var got: u8[>..] = [];
    var want: u8[>..] = [];
    got.append(prefix);
    want.append(prefix);
    let ok = base64::decode(got, text);
    let want_ok = plain_decode(want, text);
    if ok != want_ok || got != want { abort(str("decode ", text, ": ", ok, " ", got)); }
    checks++;
}

fn check_bytes(bytes: u8[:]) {
    var text: u8[>..] = [];
    var want: u8[>..] = [];
    text.append("pre");
    base64::encode(text, bytes);
    plain_encode(want, bytes);
    if text[3..] != want || text[..3] != "pre" { abort(str("encode of ", bytes.len, " bytes")); }
    var back: u8[>..] = [];
    back.append("x");
    if !base64::decode(back, text[3..]) || back[1..] != bytes || back[0] != 'x' {
        abort(str("round trip of ", bytes.len, " bytes"));
    }
    checks++;
}

fn main() {
    // RFC 4648 §10.
    let vectors = ["", "f", "fo", "foo", "foob", "fooba", "foobar"];
    var line: u8[>..] = [];
    for v, i in vectors {
        var text: u8[>..] = [];
        base64::encode(text, v);
        var back: u8[>..] = [];
        let ok = base64::decode(back, text);
        format(line, if i > 0 { " " } else { "" }, "\"", text, "\"", if ok && back == v { "" } else { "!" });
    }
    print(line);

    // Every byte value, in and out of each position of a group.
    var all: u8[>..] = [];
    for b in 256 { all.push(b as u8); }
    var text: u8[>..] = [];
    base64::encode(text, all);
    print(text[..24], "...", text[text.len - 8..], " ", text.len);
    check_bytes(all);

    var r = rng { 2024 };
    var bytes: u8[>..] = [];
    for round in 2 {
        for n in 200 {
            bytes.clear();
            for i in n { bytes.push(r.rand_int(256) as u8); }
            check_bytes(bytes);
        }
    }
    for n in [1000, 1001, 1002, 4099] {
        bytes.clear();
        for i in n { bytes.push(r.rand_int(256) as u8); }
        check_bytes(bytes);
    }

    // Every byte at every position of the texts of 0 to 6 bytes and of 48,
    // which is 64 characters.
    var mutated: u8[>..] = [];
    for n in [0, 1, 2, 3, 4, 5, 6, 48] {
        bytes.clear();
        for i in n { bytes.push(r.rand_int(256) as u8); }
        text.clear();
        base64::encode(text, bytes);
        check_text(text, "");
        for p in text.len {
            for c in 256 {
                mutated.clear();
                mutated.append(text);
                mutated[p] = c as u8;
                check_text(mutated, "ab");
            }
        }
    }
    // Alphabet characters and '=', the '=' often at the end, at every
    // length to 16; and short runs of any bytes.
    for round in 20000 {
        let n = r.rand_int(17);
        mutated.clear();
        for i in n {
            let k = r.rand_int(72);
            mutated.push(if k >= 64 || (i >= n - 3 && r.rand_int(2) == 0) { '=' } else { ALPHABET[k] });
        }
        check_text(mutated, "");
    }
    for round in 5000 {
        let n = r.rand_int(13);
        mutated.clear();
        for i in n { mutated.push(r.rand_int(256) as u8); }
        check_text(mutated, "");
    }
    print(checks, " checks against the plain codec");

    // Padding and lengths, by hand: true when valid, and what was appended.
    let cases = ["Zg==", "Zm8=", "Zg=", "Zg", "Z", "Z===", "====", "=Zg=", "Zm=v", "Zg==Zg==",
                 "Zh==", "Zm9=", "Zm9vYg==", "Zm9vYg", "Zm9v\n", "Zm 9v", "Zm9-", "Zm9_"];
    for c in cases {
        var out: u8[>..] = [];
        out.append("<");
        let ok = base64::decode(out, c);
        out.append(">");
        line.clear();
        format_replaced(line, c, "\n", "\\n");
        format(line, " ", ok, " ", out);
        print(line);
    }
}
