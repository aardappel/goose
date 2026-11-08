// error: may point at t, a reduction of this par for
// The loop's own binder is read out of the sequence: a reference to the
// reduction read through it reads the variable, whose chunks each keep a
// total of their own (docs/design/child_threads.md §4.6).
fn main() {
    var t = 1;
    let rs: i64&[8] = [t; 8];
    par for r in rs { t += r; }
    print(t);
}
