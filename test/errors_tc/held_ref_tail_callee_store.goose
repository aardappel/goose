// error: cannot assign b while b is still used: what it refers to may hold a reference or slice into it (§5.1)
// A reference of the element type stored into the struct by the callee,
// passed in pointing into the tail, stops the tail's assignment (§5.1).
struct B { e: u8&, r: u8[>..] }
fn f(b: B&, x: u8&) {
    b.e .= x;
    b.r = [9];
    print(b.e);
}
fn main() {
    var y: u8 = 7;
    var b = B { e: y, r: [1, 2] };
    f(b, b.r[0]);
}
