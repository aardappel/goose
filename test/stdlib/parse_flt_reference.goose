// runtime-debug: std parse_flt rounds correctly, through several paths
// (an exact multiply or divide, a whole part plus fraction sum, Eisel-Lemire,
// 128-bit and big-integer rounding). reference_parse_flt below reads the
// same text the plainest way and rounds it with float_text::round_exact
// alone, exact big-integer comparisons stepping from an estimate a few ulps
// off. The two must agree to the bit, and on the flag, for every input:
// edge cases, then a few hundred thousand generated ones biased to what the
// parser decides on (signs, points, exponents, lengths around 16 to 19
// digits, runs of 0 and 9, a stray byte).
import std;

fn reference_parse_flt(s: u8[:]) -> f64, bool {
    var i = 0;
    var neg = false;
    if s.len > 0 && (s[0] == '-' || s[0] == '+') {
        neg = s[0] == '-';
        i = 1;
    }
    let start = i;
    var digits = 0;
    var frac_digits = 0;
    while i < s.len && s[i] >= '0' && s[i] <= '9' {
        digits += 1;
        i += 1;
    }
    if i < s.len && s[i] == '.' {
        i += 1;
        while i < s.len && s[i] >= '0' && s[i] <= '9' {
            digits += 1;
            frac_digits += 1;
            i += 1;
        }
    }
    if digits == 0 { return 0.0, false; }
    let end = i;
    // The value is D * 10^e10, D the digits in s[start..end].
    var e10 = -frac_digits;
    if i < s.len && (s[i] == 'e' || s[i] == 'E') {
        i += 1;
        var eneg = false;
        if i < s.len && (s[i] == '-' || s[i] == '+') {
            eneg = s[i] == '-';
            i += 1;
        }
        var e = 0;
        var edigits = 0;
        while i < s.len && s[i] >= '0' && s[i] <= '9' {
            if e < 100000 { e = e * 10 + (s[i] - '0'); }
            edigits += 1;
            i += 1;
        }
        if edigits == 0 { return 0.0, false; }
        e10 += if eneg { -e } else { e };
    }
    if i != s.len { return 0.0, false; }
    // The first 19 significant digits, scaled by the rest and e10, are the
    // estimate. D * 10^e10 is below 10^(sig + e10) and at least a tenth of
    // that, which settles the values far beyond the doubles' range.
    var mant: u64 = 0;
    var kept = 0;
    var sig = 0;
    for c in s[start..end] {
        if c == '.' || (sig == 0 && c == '0') { continue; }
        sig += 1;
        if kept < 19 {
            mant = mant * 10 + (c - '0');
            kept += 1;
        }
    }
    var v = 0.0;
    if sig > 0 && sig + e10 > 310 {
        v = 1.0 / 0.0;
    } else if sig > 0 && sig + e10 >= -324 {
        var est: f64 = mant;
        var e = e10 + sig - kept;
        while e > 22 { est *= 1e22; e -= 22; }
        while e < -22 { est /= 1e22; e += 22; }
        while e > 0 { est *= 10.0; e -= 1; }
        while e < 0 { est /= 10.0; e += 1; }
        v = float_text::round_exact(s, start, end, e10, est);
    }
    if neg { v = -v; }
    return v, true;
}

// Equal values, and a zero's sign too: what tells two results apart, as
// parse_flt never makes a NaN.
fn same(a: f64, b: f64) -> bool {
    if a != b { return false; }
    return a != 0.0 || (1.0 / a < 0.0) == (1.0 / b < 0.0);
}

var rng: u64 = 0x9E3779B97F4A7C15;
fn below(n: i64) -> i64 {
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return (rng % n as u64) as i64;
}

let ALPHABET = "+-.eE0123456789 x/:";

fn push_digits(s: u8[>..]&, n: i64) {
    let mode = below(8);
    for k in n {
        var c: u8 = ('0' + below(10)) as u8;
        if mode == 0 || (mode == 2 && k > 0) { c = '0'; }
        else if mode == 1 { c = '9'; }
        s.push(c);
    }
}

fn generate(s: u8[>..]&) {
    s.clear();
    let shape = below(16);
    if shape == 0 {
        for k in below(12) { s.push(ALPHABET[below(ALPHABET.len)]); }
        return;
    }
    let sign = below(4);
    if sign == 1 { s.push('-'); } else if sign == 2 { s.push('+'); }
    if shape == 1 {
        // 16 to 21 digits with the point anywhere or nowhere.
        let n = 16 + below(6);
        let at = below(n + 2);
        for k in n {
            if k == at { s.push('.'); }
            s.push(('0' + below(10)) as u8);
        }
        return;
    }
    var ilen = below(8);
    if below(4) == 0 { ilen = below(26); }
    push_digits(s, ilen);
    if below(3) != 0 {
        s.push('.');
        var flen = below(12);
        if below(4) == 0 { flen = below(26); }
        push_digits(s, flen);
    }
    if below(5) == 0 {
        s.push(if below(2) == 0 { 'e' } else { 'E' });
        let es = below(3);
        if es == 1 { s.push('-'); } else if es == 2 { s.push('+'); }
        push_digits(s, below(5));
    }
    if below(12) == 0 {
        let at = below(s.len + 1);
        let c = ALPHABET[below(ALPHABET.len)];
        if at == s.len { s.push(c); } else { s[at] = c; }
    }
}

fn main() {
    let cases: (const u8[:])[] = ["0", "-0", "+0.0", "1", "1.", ".5", "-.5", ".", "-", "+", "", "e5",
                                  "1e", "1e+", "1e5", "1E-5", "-1.5e2", "123.456", "0.1", "0.3",
                                  "0.7374928303", "9007199254740993", "999999999999999999",
                                  "9223372036854775807", "9999999999999999999",
                                  "18446744073709551615", "1234567890123456789012345",
                                  "0.1234567890123456789", "1e22", "1e23", "2.5e-3", "1e400",
                                  "-1e400", "1e-400", "12a", " 1", "1 ", "1.2.3", "--1", "0x10",
                                  "98.91739066979865", "110.35759859866259", "114.80299819254557",
                                  "103.56484075001073", "9223372036854775808", "9539625884325383047",
                                  "1.7976931348623157e308", "1.7976931348623159e308",
                                  "2.2250738585072011e-308", "4.9406564584124654e-324",
                                  "2.4703282292062327e-324", "2.4703282292062328e-324",
                                  "+000123.4560000", "9007199254740993.00000000000000000001"];
    var bad = 0;
    for c in cases {
        let v, ok = parse_flt(c);
        let rv, rok = reference_parse_flt(c);
        if ok != rok || !same(v, rv) { bad++; }
        print("\"", c, "\" ", v, " ", ok);
    }
    var s: u8[>..] = [];
    var valid = 0;
    let count = 300000;
    for k in count {
        generate(s);
        let v, ok = parse_flt(s);
        let rv, rok = reference_parse_flt(s);
        if ok { valid++; }
        if ok != rok || !same(v, rv) {
            bad++;
            print("differs: \"", s, "\" ", v, " ", ok, " against ", rv, " ", rok);
        }
    }
    print(count, " generated, ", valid, " valid; ", bad, " differ");
}
