// error: a par for's iterations each read their element of the sequence
// Each iteration reads its element of a, and writes b[k], an element of the
// same array one further on: the next iteration's
// (docs/design/child_threads.md §4.4).
fn main() {
    var xs: i64[9] = [1, 2, 3, 4, 5, 6, 7, 8, 9];
    let a = xs[0..8];
    let b = xs[1..9];
    par for v, k in a { b[k] = v + 1; }
    print(xs);
}
