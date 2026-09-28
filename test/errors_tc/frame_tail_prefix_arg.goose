// error: cannot reference a resizable value nested in a variable-size prefix or an ADT payload; reference the owning variable instead
// Nor does the tail of a frame object behind a variable-size prefix bind to
// a reference parameter (§4.1), reached through a reference to its owner or
// not (C.2).
struct Stack { n: i64, items: i64[>..<] }
struct Named { name: u8[], stack: Stack }
fn grow(xs: i64[>..<]&) { xs.push(2); }
fn fill(v: Named&) { grow(v.stack.items); }
fn main() {
    var v = Named { name: "x", stack: Stack { n: 0, items: [1] } };
    fill(v);
}
