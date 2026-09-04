// `in cpool` says the offset spans cpool, so a reference into another array
// cannot be encoded in it whatever the destination is (§3.9).
struct CNode { key: i32, next: CNode&<u32 in cpool>? }
var cpool: CNode[>..] = [];
var cspare: CNode[>..] = [];
fn main() {
    cpool.push(CNode { key: 1 });
    cspare.push(CNode { key: 2 });
    cpool[0].next .= cspare[0];
}
