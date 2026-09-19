// A for loop over an integer count reads a reference count as its pointee
// (§3.8, §6.5), once, before the first iteration, and its variable has the
// pointee's type: a reference variable, a written &x, a narrowed optional, a
// reference parameter (inlined at -O2), an untyped parameter bound to a
// reference, a const reference, a reference field, an element of an array of
// references, a returned reference, and a for binding by reference over i32
// elements and over varint elements, which decode to i64. Each iteration
// adds its own bit from P, so a sum names the iterations that ran, and a
// count past P's length stops at the bounds check.
struct H { r: i64& }

let P = [1, 2, 4, 8, 16, 32, 64, 128];
let W = 4;

fn tally(n: u8&) -> i64 {
    var s = 0;
    for i in n {
        s += P[i];
        if i == 0 { print(i - 1); }                 // 255: i is a u8
    }
    return s;
}

fn span(n) -> i64 {
    var s = 0;
    for i in n { s += P[i]; }
    return s;
}

fn at(xs: i64[]&, i: i64) -> i64& { return xs[i]; }

fn main() {
    var a = 3;
    let r: i64& = a;
    var s1 = 0;
    var s2 = 0;
    for i in r { s1 += P[i]; }
    for i in &a { s2 += P[i]; }
    var o: i64? = a;
    var s3 = 0;
    if o { for i in o { s3 += P[i]; } }
    print(s1, " ", s2, " ", s3);                    // 7 7 7

    var b: u8 = 5;
    let rw: const i64& = W;
    var s4 = 0;
    for i in rw { s4 += P[i]; }
    print(tally(b), " ", span(&a), " ", span(a), " ", s4);   // 255, then 31 7 7 15

    // The count is read once: writes to its pointee in the body, directly or
    // through the reference, do not change how often the loop runs.
    var c = 3;
    let rc: i64& = c;
    var s5 = 0;
    var s6 = 0;
    for i in rc { c += 1; s5 += P[i]; }
    for i in rc { rc -= 1; s6 += P[i]; }
    print(s5, " ", s6, " ", c);                     // 7 63 0

    // A negative count runs no iterations.
    var neg = -5;
    let rn: i64& = neg;
    for i in rn { print(P[i]); }

    let h = H { r: a };
    let ys: i64[] = [5, 6, 7];
    let refs: (i64&)[2] = [a, ys[2]];
    var s7 = 0;
    var s8 = 0;
    var s9 = 0;
    for i in h.r { s7 += P[i]; }
    for i in refs[1] { s8 += P[i]; }
    for i in at(ys, 1) { s9 += P[i]; }
    print(s7, " ", s8, " ", s9);                    // 7 127 63

    var xs: i32[3] = [1, 2, 3];
    var s10 = 0;
    for &x in xs { for i in x { x += 1; s10 += P[i]; } }
    print(xs, " ", s10);                            // [2, 4, 6] 11

    // 130 takes two bytes as a varint.
    let v: varint[] = [3, 130, -2];
    let ones = [1; 130];
    var ks: i64[>..] = [];
    for x in v {
        var k = 0;
        for i in x { k += ones[i]; }
        ks.push(k);
    }
    print(ks);                                      // [3, 130, 0]
}
