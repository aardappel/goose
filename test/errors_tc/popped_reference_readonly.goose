// error: storing a read-only reference of type const i64& in a slot of type i64&
// A popped element is as writable as the slot it leaves (§9.5): a const one
// stays read-only.
fn main() {
    let xs: i64[4] = [1, 2, 3, 4];
    var stack: (const i64&)[>..<] = [];
    stack.push(xs[1]);
    let r: i64& = stack.pop();
    r = 5;
    print(xs);
}
