// error: cannot clear gsn while r
// error: is still used: it may refer into gsn
// A relative reference points within the array holding it (§3.9), which is
// grow-shrink here: loaded out of a field, it still points into gsn.
struct N { next: N&<u32>, v: i64 }
fn main() {
    var gsn: N[>..<] = [];
    gsn.push(N { next: self, v: 7 });
    let r: N& = gsn[0].next;
    gsn.clear();
    print(r.v);
}
