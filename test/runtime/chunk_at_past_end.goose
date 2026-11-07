// chunk_at's bounds lie within the array: one past its length aborts.
fn main() {
    let a:      i64[5] = [1, 2, 3, 4, 5];
    let bounds: i64[3] = [0, 3, 6];
    print(a.chunk_at(bounds, 0));        // [1, 2, 3]
    print(a.chunk_at(bounds[..], 1));    // Aborts: 6 is past a's 5 elements.
}
