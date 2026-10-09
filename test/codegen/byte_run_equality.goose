// Equality and copies of short element runs (§4.5, §4.2), whose code
// compares and copies whole words: static-capacity limited arrays of every
// capacity around a word boundary compared at each length and with each
// element differing, slices of every short length, and copies of every
// short length into a limited array, a grow-only array and a limited
// array's own slots.

struct Key { name: u8[..12], id: u16 }

// Every length 0..k of a u8[..k] against every same-length one differing in
// one byte, against a longer one, and against itself; the count of equal
// pairs found, which is k + 1 when the comparison is right.
fn check_u8_3() -> i64 {
    var hits = 0;
    for n in 0..4 {
        var a: u8[..3] = [];
        for i in n { a.push((i * 7 + 1) as! u8); }
        for at in 0..n {
            var b = a;
            b[at] = b[at] + 1;
            if a == b { hits += 100; }
            if !(a != b) { hits += 100; }
        }
        if n < 3 {
            var c = a;
            c.push(0);
            if a == c { hits += 100; }
        }
        let d = a;
        if a == d { hits++; }
    }
    return hits;
}

fn check_u8_7() -> i64 {
    var hits = 0;
    for n in 0..8 {
        var a: u8[..7] = [];
        for i in n { a.push((i * 7 + 1) as! u8); }
        for at in 0..n {
            var b = a;
            b[at] = b[at] + 1;
            if a == b { hits += 100; }
        }
        if n < 7 {
            var c = a;
            c.push(0);
            if a == c { hits += 100; }
        }
        let d = a;
        if a == d { hits++; }
    }
    return hits;
}

fn check_u8_9() -> i64 {
    var hits = 0;
    for n in 0..10 {
        var a: u8[..9] = [];
        for i in n { a.push((i * 7 + 1) as! u8); }
        for at in 0..n {
            var b = a;
            b[at] = b[at] ^ 128;
            if a == b { hits += 100; }
        }
        if n < 9 {
            var c = a;
            c.push(0);
            if a == c { hits += 100; }
        }
        let d = a;
        if a == d { hits++; }
    }
    return hits;
}

fn check_u8_16() -> i64 {
    var hits = 0;
    for n in 0..17 {
        var a: u8[..16] = [];
        for i in n { a.push((i * 7 + 1) as! u8); }
        for at in 0..n {
            var b = a;
            b[at] = b[at] ^ 1;
            if a == b { hits += 100; }
        }
        if n < 16 {
            var c = a;
            c.push(0);
            if a == c { hits += 100; }
        }
        let d = a;
        if a == d { hits++; }
    }
    return hits;
}

fn check_u8_21() -> i64 {
    var hits = 0;
    for n in 0..22 {
        var a: u8[..21] = [];
        for i in n { a.push((i * 7 + 1) as! u8); }
        for at in 0..n {
            var b = a;
            b[at] = b[at] ^ 64;
            if a == b { hits += 100; }
        }
        if n < 21 {
            var c = a;
            c.push(0);
            if a == c { hits += 100; }
        }
        let d = a;
        if a == d { hits++; }
    }
    return hits;
}

fn check_u8_64() -> i64 {
    var hits = 0;
    for n in 0..65 {
        var a: u8[..64] = [];
        for i in n { a.push((i * 7 + 1) as! u8); }
        for at in 0..n {
            var b = a;
            b[at] = b[at] ^ 2;
            if a == b { hits += 100; }
        }
        if n < 64 {
            var c = a;
            c.push(0);
            if a == c { hits += 100; }
        }
        let d = a;
        if a == d { hits++; }
    }
    return hits;
}

fn check_u8_70() -> i64 {
    var hits = 0;
    for n in 0..71 {
        var a: u8[..70] = [];
        for i in n { a.push((i * 7 + 1) as! u8); }
        for at in 0..n {
            var b = a;
            b[at] = b[at] ^ 4;
            if a == b { hits += 100; }
        }
        let d = a;
        if a == d { hits++; }
    }
    return hits;
}

// Wider elements: the byte count is the length times the element size.
fn check_u16_5() -> i64 {
    var hits = 0;
    for n in 0..6 {
        var a: u16[..5] = [];
        for i in n { a.push((i * 1000 + 3) as! u16); }
        for at in 0..n {
            var b = a;
            b[at] = b[at] ^ 256;
            if a == b { hits += 100; }
            var c = a;
            c[at] = c[at] ^ 1;
            if a == c { hits += 100; }
        }
        let d = a;
        if a == d { hits++; }
    }
    return hits;
}

fn check_i64_3() -> i64 {
    var hits = 0;
    for n in 0..4 {
        var a: i64[..3] = [];
        for i in n { a.push(i * 1000000007 - 5); }
        for at in 0..n {
            var b = a;
            b[at] = b[at] ^ (1 << 62);
            if a == b { hits += 100; }
        }
        let d = a;
        if a == d { hits++; }
    }
    return hits;
}

// Floats compare by value: -0.0 equals 0.0 and NaN equals nothing.
fn check_floats() -> i64 {
    var hits = 0;
    let z: f64[..4] = [0.0, 1.0];
    let nz: f64[..4] = [-0.0, 1.0];
    if z == nz { hits++; }
    let nan = 0.0 / 0.0;
    let n1: f64[..4] = [nan];
    let n2 = n1;
    if n1 == n2 { hits += 100; }
    return hits;
}

// Slices of every length up to 40, equal and differing at each position;
// the compared runs start at every offset of a word.
fn check_slices() -> i64 {
    var hits = 0;
    var buf: u8[>..] = [];
    for i in 100 { buf.push((i * 13 + 5) as! u8); }
    var other: u8[>..] = copy(buf);
    for off in 0..8 {
        for n in 0..41 {
            let a = buf[off..off + n];
            let b = other[off + 3..off + 3 + n];
            let c = buf[off + 16..off + 16 + n];
            if a == b && n > 0 { hits += 100000; }
            if a == c && n > 0 { hits += 100000; }
            if a == buf[off..off + n] { hits++; }
            for at in 0..n {
                other[off + at] = other[off + at] ^ 32;
                if a == other[off..off + n] { hits += 100000; }
                other[off + at] = other[off + at] ^ 32;
            }
            if a == other[off..off + n] { hits++; }
        }
    }
    return hits;
}

// A limited array against a slice, and two limited arrays of different
// capacities: compared as slices.
fn check_mixed() -> i64 {
    var hits = 0;
    let k: u8[..16] = "item_1234";
    let s: u8[] = "item_1234";
    if k == s[..] { hits++; }
    if k == "item_1234" { hits++; }
    if k == "item_1235" { hits += 100; }
    if k == "item_123" { hits += 100; }
    let k2: u8[..32] = "item_1234";
    if k == k2[..] { hits++; }
    return hits;
}

// Structs holding limited arrays compare them field by field.
fn check_keys() -> i64 {
    var hits = 0;
    let a = Key { name: "alpha", id: 7 };
    let b = Key { name: "alpha", id: 7 };
    let c = Key { name: "alphb", id: 7 };
    let d = Key { name: "alpha", id: 8 };
    let e = Key { name: "alph", id: 7 };
    if a == b { hits++; }
    if a == c { hits += 100; }
    if a == d { hits += 100; }
    if a == e { hits += 100; }
    return hits;
}

// Copies of every short length: into a limited array (directly, through a
// reference, and as a binding's initializer), appended to a grow-only array,
// and from a limited array's own slots, shifted down and up.
fn copy_into(l: u8[..40]&, s: u8[:]) { l = s; }

fn check_copies() -> i64 {
    var hits = 0;
    var src: u8[>..] = [];
    for i in 64 { src.push((i * 29 + 11) as! u8); }
    for n in 0..41 {
        var l: u8[..40] = [];
        l = src[3..3 + n];
        if l == src[3..3 + n] && l.len == n { hits++; }
        copy_into(l, src[5..5 + n]);
        if l == src[5..5 + n] { hits++; }
        let m: u8[..40] = src[7..7 + n];
        if m == src[7..7 + n] { hits++; }
        var g: u8[>..] = [];
        g.append(src[1..1 + n]);
        g.append(src[2..2 + n]);
        if g[..n] == src[1..1 + n] && g[n..] == src[2..2 + n] { hits++; }
        // The source lies in the destination's own slots.
        l = src[..n];
        if n > 0 {
            let tail = l[1..];
            l = tail;
            if l == src[1..n] { hits++; }
        } else {
            hits++;
        }
        var w: u8[..40] = src[..40];
        let head = w[..n];
        w = head;
        if w == src[..n] { hits++; }
    }
    return hits;
}

fn main() {
    print(check_u8_3(), " ", check_u8_7(), " ", check_u8_9(), " ", check_u8_16());
    print(check_u8_21(), " ", check_u8_64(), " ", check_u8_70());
    print(check_u16_5(), " ", check_i64_3(), " ", check_floats());
    print(check_slices(), " ", check_mixed(), " ", check_keys());
    print(check_copies());
}
