// error: copying a value of type Holder, which contains self-relative references, is not supported: its links, read out of this copy, would be measured from the copy; read them where the value is stored
// A reference parameter given a field of the popped copy would follow its
// links from the copy (§3.9).
struct Ring { next: Ring&<u32>, v: i64 }
struct Holder { r: Ring, k: i64 }
fn follow(r: Ring&) -> i64 { return r.next.v; }
fn main() {
    var hs: Holder[>..] = [];
    hs.push(Holder { r: Ring { next: self, v: 1 }, k: 2 });
    print(follow(hs.pop().r));
}
