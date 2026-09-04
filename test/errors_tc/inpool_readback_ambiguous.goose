// Two global containers can hold an RNode, so a reference read back out of
// the slot array only outlives the global scope (§9.5) and is not known to
// point into rpool (§3.9).
struct RNode { key: i32, next: RNode&<u32 in rpool>? }
var rpool: RNode[>..] = [];
var rspare: RNode[>..] = [];
var rslots: (RNode&?)[>..] = [];
fn main() {
    rpool.push(RNode { key: 1 });
    rspare.push(RNode { key: 2 });
    rslots.push(&rpool[0]);
    let n = rslots[0];
    assert(n);
    rpool[0].next .= n;
}
