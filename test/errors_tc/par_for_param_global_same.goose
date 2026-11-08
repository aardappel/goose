// error: src is or may point into storage partitioned by this par for
// f's two parameters are given two classes, but g passes its own parameter
// beside a global, which g's caller makes the same array: what a class
// stands for may be any global beside it (docs/design/child_threads.md §4.4).
var GA: i64[8] = [1, 2, 3, 4, 5, 6, 7, 8];
fn f(dst: i64[:], src: const i64[:]) { par for k in dst.len { dst[k] = src[(k + 1) % 8] + 1; } }
fn g(p: i64[:]) { f(p, GA); }
fn main() {
    g(GA);
    print(GA);
}
