// chunk_at's bounds do not fall: a run that would end before it starts
// aborts where it is taken (§9.3), after the runs before it.
fn main() {
    var a: i64[>..] = [1, 2, 3, 4, 5, 6];
    let bounds: i64[>..] = [0, 2, 4, 3, 6];
    for k in bounds.len - 1 { print(a.chunk_at(bounds, k)); }
}
