// error: copying a value of type S, which contains self-relative references, into the format overload taking it by value is not supported
// A format overload taking a T by value is given a copy (§3.7), whose
// self-relative references would still be measured from where the T lies
// (§3.9), as any other copy's: it takes such a T by reference.
struct T { x: i64 }
struct S { t: T&<u8>?, k: i64 }
struct Blob { ts: T[4], s: S }
var blobs: Blob[>..] = [];
fn format(out: u8[>..]&, s: S) {
    let t .= s.t;
    if t { format(out, t.x); }
    format(out, ",", s.k);
}
fn main() {
    blobs.push(Blob { ts: [T { x: 1 }, T { x: 2 }, T { x: 3 }, T { x: 4 }], s: S { t: null, k: 0 } });
    blobs[0].s = S { t: blobs[0].ts[2], k: 6 };
    print(blobs[0]);
}
