// A negative chunk_at index aborts, rather than read the bound before the
// first.
fn main() {
    let a:      i64[4]   = [1, 2, 3, 4];
    let bounds: i64[>..] = [0, 1, 4];
    var k = 0;
    print(a.chunk_at(bounds, k));        // [1]
    k--;
    print(a.chunk_at(bounds, k));        // Aborts: index -1, of 2 runs.
}
