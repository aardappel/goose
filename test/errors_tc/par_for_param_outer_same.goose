// error: src is or may point into storage partitioned by this par for
// As par_for_param_global_same.goose, with a variable of the function g is
// nested in beside g's parameter (docs/design/child_threads.md §4.4).
fn f(dst: i64[:], src: const i64[:]) { par for k in dst.len { dst[k] = src[(k + 1) % 8] + 1; } }
fn main() {
    var a: i64[8] = [1, 2, 3, 4, 5, 6, 7, 8];
    fn g(p: i64[:]) { f(p, a); }
    g(a);
    print(a);
}
