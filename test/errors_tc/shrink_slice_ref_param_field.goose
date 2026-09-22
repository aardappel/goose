// error: cannot assign r while p
// error: is still used: the slice it refers to may point into r
// The slice p refers to views a struct's grow-shrink field, which assigning
// the struct whole through a reference frees (§5.2).
struct Rec { name: u8[], items: i64[>..<] }
fn peek(p: i64[:]&, r: Rec&) {
    r = Rec { name: "ABCDEFGHIJKLMNOPQRSTUVWXYZ", items: [] };
    print(p[0]);
}
fn main() {
    var r = Rec { name: "a", items: [] };
    r.items.push(7);
    var s = r.items[..];
    peek(s, r);
}
