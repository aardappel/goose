// runtime-debug: A format overload for a type holding self-relative
// references takes it by reference (§3.7): it is given the storage the value
// lies in, from which its offsets are measured (§3.9), wherever the value
// lies: a field, an element, behind a reference.
struct T { x: i64 }
struct S { t: T&<u8>?, k: i64 }
struct Blob { ts: T[4], s: S, more: S[2] }
var blobs: Blob[>..] = [];
fn format(out: u8[>..]&, s: S&) {
    format(out, "S(");
    let t .= s.t;
    if t { format(out, t.x); } else { format(out, "-"); }
    format(out, ",", s.k, ")");
}
fn main() {
    blobs.push(Blob { ts: [T { x: 1 }, T { x: 2 }, T { x: 3 }, T { x: 4 }],
                      s: S { t: null, k: 0 }, more: [S { t: null, k: 0 }, S { t: null, k: 0 }] });
    blobs[0].s = S { t: blobs[0].ts[2], k: 6 };
    blobs[0].more[1] = S { t: blobs[0].ts[0], k: 7 };
    print(blobs[0]);        // Blob { [T { 1 }, T { 2 }, T { 3 }, T { 4 }], S(3,6), [S(-,0), S(1,7)] }
    print(blobs[0].s);      // S(3,6)
    let r: Blob& = blobs[0];
    print(str(r.more));     // [S(-,0), S(1,7)]
}
