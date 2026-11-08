// error: alias may point at total, a reduction of this par for
// A reference to a reduction read in the body reads the variable, whose
// chunks each keep a total of their own (docs/design/child_threads.md §4.6).
fn main() {
    var total = 1;
    let alias = &total;
    par for k in 8 { total += alias; }
    print(total);
}
