// goose-threads
// A generic function handed a nested function of a par for's body, called
// twice in that body, where what the nested function writes is the body's
// own, beside an inner par for that reads it: run is checked for the loop it
// is called in (docs/design/child_threads.md §4.3).
fn run<F>(k: i64) { F(k); }

fn main() {
    var out: i64[4] = [0, 0, 0, 0];
    par for o in 4 {
        var n = o;
        fn bump(k: i64) { n = n * 3 + k; }
        run(1, bump);
        var inner: i64[8] = [0, 0, 0, 0, 0, 0, 0, 0];
        par for k in 8 { inner[k] = k + n; }
        run(2, bump);
        var t = 0;
        for v in inner { t += v; }
        out[o] = n + t;
    }
    print(out);
}
