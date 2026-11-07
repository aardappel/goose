// error: view is or may point into storage partitioned by this par for
// view is a global `var` slice, which any function may bind: kernel's check
// is reused after retarget binds it to data, the array kernel's iterations
// own, so it stands on every array view can be given, not the one it views
// first (docs/design/child_threads.md §4.4).
const seed: i64[8] = [1, 1, 1, 1, 1, 1, 1, 1];
var data: i64[8] = [1, 1, 1, 1, 1, 1, 1, 1];
var view: const i64[:] = seed;
fn kernel(dst: i64[:]) {
    par for k in dst.len { dst[k] = view[(k + 1) % dst.len] + 1; }
}
fn retarget() { view = data; }
fn main() {
    kernel(data);
    retarget();
    kernel(data);
    var s = 0;
    for x in data { s += x; }
    print(s);
}
