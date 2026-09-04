// A relative reference's pool must be a global: a local pool has no name at
// the field's declaration, so such a structure links self-relatively (§3.9).
struct LNode { key: i32, next: LNode&<u32 in lpool>? }
fn main() {
    var lpool: LNode[>..] = [];
    lpool.push(LNode { key: 1 });
    print(lpool.len);
}
