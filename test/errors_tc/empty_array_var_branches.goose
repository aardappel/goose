// error: cannot infer the element type of []: only a var whose whole initializer is [] takes it from what goes into it first
// A var learns its element type from the first push only where [] is its
// whole initializer (§4.2), not a construct's branch.
fn main() {
    let c = true;
    var x = if c { [] } else { return; };
    x.push(1);
    print(x.len);
}
