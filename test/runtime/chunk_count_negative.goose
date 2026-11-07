// A negative count of runs aborts as a zero one does: no index lies below
// it, though as an unsigned number every index would.
fn main() {
    let a: i64[3] = [1, 2, 3];
    var n = 2;
    print(a.chunk(1, n));                // [3]
    n -= 3;
    print(a.chunk(0, n));                // Aborts.
}
