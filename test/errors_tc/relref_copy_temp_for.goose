// error: copying a value of type Ring[2], which contains self-relative references, is not supported: its links, read out of this copy, would be measured from the copy; read them where the value is stored
// A loop over a construct's copy binds its elements where the copy lies
// (§6.5, §3.9).
struct Ring { next: Ring&<u32>, v: i64 }
struct Two { a: Ring[2] }
fn main() {
    var ts: Two[>..] = [];
    ts.push(Two { a: [Ring { next: self, v: 1 }, Ring { next: self, v: 2 }] });
    ts.push(Two { a: [Ring { next: self, v: 3 }, Ring { next: self, v: 4 }] });
    let c = ts.len > 1;
    for &r in (if c { ts[0].a } else { ts[1].a }) { print(r.next.v); }
}
