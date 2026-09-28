// error: cannot pop refs: the for loop at
// error: reads refs[0] again on every iteration, which may lie in refs
// `&` of a path is that path: the loop reaches v through the reference in
// refs on every iteration (§5.2, §6.5).
struct B { v: i64[3] }
fn main() {
    var b = B { v: [1, 2, 3] };
    var c = B { v: [7, 7, 7] };
    var refs: (B&)[>..<] = [];
    refs.push(b);
    for x in &refs[0].v {
        refs.pop();
        refs.push(c);
        print(x);
    }
}
