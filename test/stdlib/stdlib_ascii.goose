// stdlib/std.goose's ascii character classes against plain definitions,
// C's <ctype.h> in the C locale, at every one of the 256 byte values; then
// each class written out, and hex_value at the bytes on either side of each
// range, where a wrapping range test would go wrong first.
import std;

fn plain_digit(c: u8) -> bool { c >= '0' && c <= '9' }
fn plain_alpha(c: u8) -> bool { (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') }

fn plain_hex(c: u8) -> i64 {
    let lower = position("0123456789abcdef", c);
    if lower >= 0 { return lower; }
    return position("0123456789ABCDEF", c);
}

fn main() {
    var checks = 0;
    var wrong = 0;
    var digits: u8[>..] = [];
    var alphas: u8[>..] = [];
    var alnums: u8[>..] = [];
    var hex:    u8[>..] = [];
    for b in 256 {
        let c = b as u8;
        if ascii::is_digit(c) != plain_digit(c) { print("is_digit ", b); wrong++; }
        if ascii::is_alpha(c) != plain_alpha(c) { print("is_alpha ", b); wrong++; }
        if ascii::is_alnum(c) != (plain_digit(c) || plain_alpha(c)) { print("is_alnum ", b); wrong++; }
        if ascii::hex_value(c) != plain_hex(c) { print("hex_value ", b); wrong++; }
        checks += 4;
        if ascii::is_digit(c) { digits.push(c); }
        if ascii::is_alpha(c) { alphas.push(c); }
        if ascii::is_alnum(c) { alnums.push(c); }
        if ascii::hex_value(c) >= 0 { hex.push(c); }
    }
    print(checks, " checks against the plain definitions, ", wrong, " wrong");
    print(digits);
    print(alphas);
    print(alnums);
    print(hex);

    var values: u8[>..] = [];
    for c in hex { format(values, " ", ascii::hex_value(c)); }
    print(values[1..]);

    // Either side of '0'-'9', 'a'-'f' and 'A'-'F', the letters past f, and
    // bytes from 0x80 up whose low seven bits are a digit or a letter.
    var edges: u8[>..] = [];
    let probes: u8[] = ['/', ':', '`', 'g', '@', 'G', 'z', 'Z', 0, 0xb0, 0xc1, 0xe1, 0xff];
    for c in probes { format(edges, " ", ascii::hex_value(c)); }
    print(edges[1..]);
    print(ascii::is_alpha('@'), " ", ascii::is_alpha('['), " ", ascii::is_alpha('`'), " ",
          ascii::is_alpha('{'), " ", ascii::is_alpha(0xc1), " ", ascii::is_digit(0xb0));
}
