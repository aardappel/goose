// error: cannot render the To payload of fixed-mode Link around a format overload for a part of it
// A fixed-mode ADT's payload is rendered from a copy where an overload runs
// among its parts (§3.7), and a copy of self-relative references would still
// measure them from where the payload was (§3.9).
struct T { x: i64 }
struct L { v: i64 }
enum Link {
    To { l: L, t: T&<u8> },
    Nowhere,
}
struct Blob { ts: T[3], link: Link }
var blobs: Blob[>..] = [];
fn format(out: u8[>..]&, l: L) { format(out, "L", l.v); }
fn main() {
    blobs.push(Blob { ts: [T { x: 1 }, T { x: 2 }, T { x: 3 }], link: Link.Nowhere });
    blobs[0].link = Link.To { l: L { v: 1 }, t: blobs[0].ts[1] };
    print(blobs[0]);
}
