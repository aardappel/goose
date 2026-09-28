// error: cannot pop rows: the for loop at
// error: reads rows[0] again on every iteration, which may lie in rows
// Indexing through a slice element: the loop checks the index against the
// slice's length, which it loads out of rows again on every iteration
// (§5.2, §6.5).
fn main() {
    var grid: i64[3][2] = [[1, 2, 3], [4, 5, 6]];
    var other: i64[3][1] = [[7, 7, 7]];
    var rows: (i64[3][:])[>..<] = [];
    rows.push(grid[..]);
    for x in rows[0][1] {
        rows.pop();
        rows.push(other[..]);
        print(x);
    }
}
