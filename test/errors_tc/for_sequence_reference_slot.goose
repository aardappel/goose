// error: cannot pop refs: the for loop at
// error: reads refs[0] again on every iteration, which may lie in refs
// The loop iterates a whole array, which is safe, but finds it through the
// reference stored in refs, which it loads again on every iteration: after
// the pop and push, out of a slot now pointing at b (§5.2, §6.5).
fn main() {
    var a: i64[>..<] = [1, 2, 3];
    var b: i64[>..<] = [4, 5, 6, 7, 8];
    var refs: (i64[>..<]&)[>..<] = [];
    refs.push(a);
    for x in refs[0] {
        refs.pop();
        refs.push(b);
        print(x);
    }
}
