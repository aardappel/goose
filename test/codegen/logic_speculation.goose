// runtime-debug: && and || whose right operand runs whatever the left operand
// gave (CodeGen::Speculatable, in a loop that can be straight-line code) give
// what short-circuit evaluation gives, and the right operands that cannot
// run that way still run only where the left lets them: an index the left
// keeps in bounds, an index nothing proves, a division by what the left
// tests, a narrowed optional, signed arithmetic and a narrowing cast (both of
// which a debug build checks), and calls. The inputs are globals main may
// change, so that no operand folds to a constant, and the tests sit in loops
// of one round that call nothing, where && and || can take the
// unconditional form.
import std;

struct P { x: i64, flag: bool }

var calls = 0;
var far = 1 << 40;
var ten = 10;
var zero = 0;
var big = 9223372036854775807;
var wide = 1000;
var fzero = 0.0;
var fone = 1.0;
var full: u8 = 255;
var seven: u32 = 7;
var holder = P { x: 1, flag: true };
var none: P? = null;
var some: P? = null;
var one = 1;

fn bump(v: bool) -> bool {
    calls += 1;
    return v;
}

fn is_letter(b: u8) -> bool { (b >= 'a' && b <= 'z') || (b >= 'A' && b <= 'Z') }

fn print_bits(v: u64, n: i64) {
    var out: u8[>..] = [];
    format_uint(out, v, 2, n, '0');
    print(out);
}

// Every combination of three booleans, five forms each, and of two small
// integers, two forms each, as bits.
fn tables() {
    var bits: u64 = 0;
    for k in 8 {
        let x = k & 1 != 0;
        let y = k & 2 != 0;
        let z = k & 4 != 0;
        bits = bits << 1 | (if x && y { 1 } else { 0 });
        bits = bits << 1 | (if x || y { 1 } else { 0 });
        bits = bits << 1 | (if x && y || z { 1 } else { 0 });
        bits = bits << 1 | (if x && (y || z) { 1 } else { 0 });
        bits = bits << 1 | (if !(x && !y) || z { 1 } else { 0 });
    }
    print_bits(bits, 40);   // 0000101000010011111100101011110110111111
    bits = 0;
    for a in 3 {
        for b in 3 {
            bits = bits << 1 | (if a < b && b != 2 { 1 } else { 0 });
            bits = bits << 1 | (if a == 1 || b > a { 1 } else { 0 });
        }
    }
    print_bits(bits, 18);   // 001101010101000000
}

// Floats, NaN among them, unsigned arithmetic, which wraps, a widening cast
// and a table a u8 indexes.
fn values() {
    var r: bool[6] = [false; 6];
    for k in one {
        let nan = fzero / fzero;
        r[0] = nan == nan && nan < 1.0;
        r[1] = fone == fone && fone * 2.0 > 1.5;
        r[2] = nan != nan || fone / fzero < 0.0;
        r[3] = full > 5 && full + 1 == 0;
        r[4] = seven == 7 && (seven as u64) << 40 > 0;
        r[5] = full < 5 || full - 1 == 254;
    }
    print(r[0], " ", r[1], " ", r[2], " ", r[3], " ", r[4], " ", r[5]);
                                                    // false true true true true true
    var table: bool[256] = [false; 256];
    table['x'] = true;
    var n = 0;
    for c in "axbxcx" { if c != 'a' && table[c] { n += 1; } }
    print(n);                                                             // 3
}

// A loop over blocks of 32 bytes, whose indices the loop keeps in bounds, and
// inlined predicates.
fn loops() {
    var text: u8[>..] = [];
    for i in 200 { text.push(if i % 7 == 0 { 'a' } else if i % 5 == 0 { 'b' } else { '-' }); }
    var pairs = 0;
    var p = 0;
    while p + 33 <= text.len {
        let p1 = text[p..][..32];
        let p2 = text[p + 1..][..32];
        for j in 32 { if p1[j] == '-' && p2[j] == 'a' { pairs += 1; } }
        p += 32;
    }
    var letters = 0;
    for c in "a_B-c9_Z!" { if c == '_' || is_letter(c) { letters += 1; } }
    var spaces = 0;
    for c in " a\tb\nc\r d" { if c == ' ' || c == '\t' || c == '\n' || c == '\r' { spaces += 1; } }
    print(pairs, " ", letters, " ", spaces);                              // 22 6 5
}

// Optional references: their null tests, and their truth.
fn optionals() {
    var r: bool[5] = [false; 5];
    for k in one {
        r[0] = none != null && some != null;
        r[1] = some != null && none == null;
        r[2] = none == null || some == null;
        r[3] = some && none;
        r[4] = some || none;
    }
    print(r[0], " ", r[1], " ", r[2], " ", r[3], " ", r[4]);   // false true true false true
}

// Right operands the left must guard.
fn guarded() {
    let xs: i64[] = [3, 1, 4];
    let arr: i64[4] = [1, 1, 1, 1];
    var r: bool[12] = [false; 12];
    for k in one {
        r[0] = far < xs.len && xs[far] == 3;
        r[1] = far >= xs.len || xs[far] == 3;
        r[2] = ten < 4 && arr[ten] == 1;
        r[3] = ten >= 4 || arr[ten] == 1;
        r[4] = zero != 0 && 10 / zero > 1;
        r[5] = zero == 0 || 10 % zero == 1;
        r[6] = none != null && none.x > 0;
        r[7] = none == null || none.flag;
        r[8] = big < 100 && big + 1 > 0;
        r[9] = big > 100 || -big - 2 < 0;
        r[10] = wide < 256 && (wide as u8) == 3;
        r[11] = wide >= 256 || (wide as u8) == 3;
    }
    var out: u8[>..] = [];
    for b in r { out.push(if b { '1' } else { '0' }); }
    print(out);                                                           // 010101010101
    let yes = calls == 0;
    let no = !yes;
    print(no && bump(true), " ", yes || bump(false), " ", calls);         // false true 0
    print(yes && bump(true), " ", no || bump(false), " ", calls);         // true false 2
}

fn main() {
    some .= holder;
    if calls > 0 {
        far = 0;
        ten = 0;
        zero = 1;
        big = 0;
        wide = 0;
        fzero = 1.0;
        fone = 2.0;
        full = 1;
        seven = 1;
        one = 2;
        none .= holder;
    }
    tables();
    values();
    loops();
    optionals();
    guarded();
}
