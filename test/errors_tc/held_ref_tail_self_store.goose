// error: cannot clear t while t is still used: a reference into it was stored there
// A struct's field holding a reference into its own tail stops the tail's
// shrink while the struct is used (§5.1).
struct B { e: u8&, r: u8[>..] }
fn main() {
    var x: u8 = 7;
    var t = B { e: x, r: [1, 2, 3] };
    t.e .= t.r[0];
    t.r.clear();
    print(t.e);
}
