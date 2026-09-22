// error: cannot call peek: it may shrink a while p is still used, and p may refer into a
// g's root is a merge of a's and b's, which only bounds what it points at, so
// g gets a root class of its own beside the one of a's view (§9.2). Inside
// peek the two classes are two arrays, but this call may pass a to both: the
// whole assignment through r frees the items p views, and p is still used
// (§5.2).
struct Rec { name: u8[], items: (const u8[:])[>..<] }
fn peek(p: (const u8[:])[:], r: Rec&) {
    r = Rec { name: "ABCDEFGHIJKLMNOPQRSTUVWXYZ", items: [] };
    print(p[0].len);
}
fn main() {
    var a = Rec { name: "a", items: [] };
    a.items.push("ok");
    var b = Rec { name: "b", items: [] };
    let c = a.items.len > 0;
    let g: Rec& = if c { a } else { b };
    peek(a.items[..], g);
}
