// error: cannot call shrink: it shrinks t while t is still used, and t may refer into t (§5.1)
// The callee uses the struct after shrinking its tail; what the caller
// stored into the struct is the call's to judge (§5.1).
struct B { e: u8&, r: u8[>..] }
fn shrink(t: B&) {
    t.r.clear();
    print(t.e);
}
fn main() {
    var x: u8 = 7;
    var t = B { e: x, r: [1, 2, 3] };
    t.e .= t.r[0];
    shrink(t);
}
