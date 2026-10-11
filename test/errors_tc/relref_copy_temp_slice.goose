// error: copying a value of type Ring[2], which contains self-relative references, is not supported: its links, read out of this copy, would be measured from the copy; read them where the value is stored
// A slice of a construct's copy views offsets measured from the copy
// (§3.10, §3.9).
struct Ring { next: Ring&<u32>, v: i64 }
struct Two { a: Ring[2] }
fn second(rs: Ring[:]) -> i64 { return rs[1].next.v; }
fn main() {
    var ts: Two[>..] = [];
    ts.push(Two { a: [Ring { next: self, v: 1 }, Ring { next: self, v: 2 }] });
    ts.push(Two { a: [Ring { next: self, v: 3 }, Ring { next: self, v: 4 }] });
    let c = ts.len > 1;
    print(second((if c { ts[0].a } else { ts[1].a })[..]));
}
