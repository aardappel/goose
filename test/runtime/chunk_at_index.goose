// a.chunk_at(bounds, k) has a run between each two neighboring bounds, so
// bounds.len - 1 runs: k = bounds.len - 1 has no end to run to (§9.3).
fn main() {
    let a:      i64[5] = [1, 2, 3, 4, 5];
    let bounds: i64[3] = [0, 2, 5];
    for k in 2 { print(a.chunk_at(bounds, k)); }
    print(a.chunk_at(bounds, bounds.len - 1));    // Aborts: index 2, of 2 runs.
}
