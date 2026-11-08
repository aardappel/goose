// error: s is or may point into storage partitioned by this par for
// As par_for_root_component.goose, with the component partitioned through a
// slice of it (docs/design/child_threads.md §4.4).
root struct S { first: i64[>..], second: i64[>..] }
fn main() {
    var s = S { first: [1, 2, 3, 4, 5, 6, 7, 8], second: [0, 0] };
    let v: i64[:] = s.first;
    par for k in v.len { v[k] = s.first[0] + 1; }
    print(s.first);
}
