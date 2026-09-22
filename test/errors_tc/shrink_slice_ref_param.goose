// error: cannot assign r while p
// error: is still used: the slice it refers to may point into r
// p refers to the caller's slice variable, whose slice points into r's
// grow-shrink array: assigning r whole is a shrink while p is still used
// (§5.2), whatever a slice itself can be an element of.
struct Rec { name: u8[], items: (const u8[:])[>..<] }
fn peek(p: (const u8[:])[:]&, r: Rec&) {
    r = Rec { name: "ABCDEFGHIJKLMNOPQRSTUVWXYZ", items: [] };
    print(p[0].len);
}
fn main() {
    var r = Rec { name: "a", items: [] };
    r.items.push("ok");
    var s = r.items[..];
    peek(s, r);
}
