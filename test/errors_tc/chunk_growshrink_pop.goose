// error: cannot pop q while c (bound at
// A chunk of a grow-shrink array is a view into it, held by a variable,
// which must be dead when the array next shrinks (§5.2).
fn main() {
    var q: i64[>..<] = [1, 2, 3, 4];
    let c = q.chunk(0, 2);
    q.pop();
    print(c);
}
