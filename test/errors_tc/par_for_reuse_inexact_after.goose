// error: src is or may point into storage partitioned by this par for
// The first call passes two arrays, the second one array twice, maybe:
// what the specialization's par for may do is judged once every call that
// reuses it has been seen (docs/design/child_threads.md §4.4).
fn choose(a: i64[:], b: i64[:], c: bool) -> i64[:] { if c { a } else { b } }
fn transform(dst: i64[:], src: const i64[:]) {
    par for k in dst.len { dst[k] = src[(k + 1) % dst.len] + 1; }
}
fn main() {
    var a: i64[8] = [1, 2, 3, 4, 5, 6, 7, 8];
    var b: i64[8] = [10, 20, 30, 40, 50, 60, 70, 80];
    transform(a, b);
    transform(a, choose(a, b, true));
    print(a);
}
