// error: grid is partitioned by this par for, each iteration owning grid.chunk(i, 4)
// R2 (§4.4): the outer par for's iterations each own a chunk of grid, which
// a par for in its body may not read beside it.
fn main() {
    var grid: i64[16] = [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0];
    par for i in 4 {
        let row = grid.chunk(i, 4);
        var mine: i64[4] = [0, 0, 0, 0];
        par for j in 4 { mine[j] = grid[j]; }
        row[0] = mine[0];
    }
    print(grid);
}
