// error: cannot call shrink, which shrinks t while t is still used: a reference into it was stored there
// The same where a callee shrinks the tail through a reference: the call
// judges the struct's own store record (§5.1).
struct B { e: u8&, r: u8[>..] }
fn shrink(t: B&) { t.r.clear(); }
fn main() {
    var x: u8 = 7;
    var t = B { e: x, r: [1, 2, 3] };
    t.e .= t.r[0];
    shrink(t);
    print(t.e);
}
