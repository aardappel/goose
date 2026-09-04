// `self` in an `in spool` field stores the value's own offset in spool, which
// a literal built into another array does not have (§3.9).
struct SNode { key: i32, next: SNode&<u32 in spool> }
var spool: SNode[>..] = [];
var sother: SNode[>..] = [];
fn main() {
    sother.push(SNode { key: 1, next: self });
    print(sother.len);
}
