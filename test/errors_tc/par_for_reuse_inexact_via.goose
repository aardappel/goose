// error: src is or may point into storage partitioned by this par for
// transform's arguments are another function's parameters, whose second
// call may pass one array to both: transform's classes may then be one
// array too (docs/design/child_threads.md §4.4).
fn choose(a: i64[:], b: i64[:], c: bool) -> i64[:] { if c { a } else { b } }
fn transform(dst: i64[:], src: const i64[:]) {
    par for k in dst.len { dst[k] = src[(k + 1) % dst.len] + 1; }
}
fn both(x: i64[:], y: i64[:]) { transform(x, y); }
fn main() {
    var a: i64[8] = [1, 2, 3, 4, 5, 6, 7, 8];
    var b: i64[8] = [10, 20, 30, 40, 50, 60, 70, 80];
    both(a, b);
    both(a, choose(a, b, true));
    print(a);
}
