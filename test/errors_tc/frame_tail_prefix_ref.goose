// error: cannot reference a resizable value nested in a variable-size prefix or an ADT payload; reference the owning variable instead
// A frame object behind a variable-size prefix is bytes of its owner (C.2),
// so its own tail has no header to reference either.
struct Stack { n: i64, items: i64[>..<] }
struct Named { name: u8[], stack: Stack }
fn main() {
    var v = Named { name: "x", stack: Stack { n: 0, items: [1] } };
    let r = &v.stack.items;
    r.push(2);
}
