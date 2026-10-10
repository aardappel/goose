// A value of a narrow integer type lies within that type's range wherever it
// comes from: a byte read out of an array indexes a 256-entry table, and the
// byte shifted right by two a 64-entry one. A signed byte may be negative.
import std;

fn byte_table(tab: i64[256]&, s: u8[:]) -> i64 {
    var t = 0;
    for i in s.len { t += tab[s[i]]; }        // bce:elide
    return t;
}

fn high_bits(tab: i64[64]&, s: u8[:]) -> i64 {
    var t = 0;
    for i in s.len { t += tab[s[i] >> 2]; }   // bce:elide
    return t;
}

fn word_bits(tab: i64[64]&, w: u32[:]) -> i64 {
    var t = 0;
    for x in w { t += tab[(x >> 18) & 63]; }  // bce:elide
    return t;
}

// (k is opaque, so the inner index is kept as well.)
fn signed_byte(tab: i64[256]&, s: i8[:], k: i64) -> i64 {
    return tab[s[k]];                         // bce:keep
}

fn wide_value(tab: i64[256]&, s: u16[:], k: i64) -> i64 {
    return tab[s[k]];                         // bce:keep
}

fn main() {
    var tab: i64[256] = [0; 256];
    for i in 256 { tab[i] = i; }
    var small: i64[64] = [0; 64];
    for i in 64 { small[i] = 1000 + i; }
    var bytes: u8[>..] = [];
    bytes.append("Az~");
    var words: u32[>..] = [262144, 4294967295];
    var sbytes: i8[>..] = [3, 100];
    var shorts: u16[>..] = [7, 200];
    print(byte_table(tab, bytes));    // 65 + 122 + 126 = 313
    print(high_bits(small, bytes));   // 1016 + 1030 + 1031 = 3077
    print(word_bits(small, words));   // 1001 + 1063 = 2064
    var ks: i64[>..] = [1];
    print(signed_byte(tab, sbytes, ks[0])); // 100
    print(wide_value(tab, shorts, ks[0]));  // 200
}
