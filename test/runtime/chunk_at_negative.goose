// A bound below zero aborts, though the run it starts ends in range.
fn main() {
    let a:      i64[4] = [1, 2, 3, 4];
    let bounds: i64[2] = [-1, 2];
    print(a.chunk_at(bounds, 0));        // Aborts.
}
