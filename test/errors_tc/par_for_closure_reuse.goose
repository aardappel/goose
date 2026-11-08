// error: cannot assign n in a par for
// run is checked first for the outer loop, where bump's n is the body's own,
// and called again in the inner loop, where n is from outside its body: a
// function handed a value that names variables of its own is checked for
// each loop it is called in (docs/design/child_threads.md §4.3 R1).
fn run<F>(k: i64) { F(k); }
fn main() {
    var out: i64[1] = [0];
    par for o in 1 {
        var n = 0;
        fn bump(k: i64) { n = (n * 3 + k) % 1000003; }
        run(1, bump);
        par for k in 1000000 { run(k, bump); }
        out[o] = n;
    }
    print(out);
}
