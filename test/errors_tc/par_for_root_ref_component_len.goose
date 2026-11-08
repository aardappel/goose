// error: view is or may point into storage partitioned by this par for
// view.first is a reference to the first row's cells, whose length lies in
// the row an iteration owns and clears: a nested function reading
// view.first.len reads it (docs/design/child_threads.md §4.4).
struct Row { cells: i64[..4] }
root struct View { first: i64[..4]& }
fn main() {
    var rows: Row[4] = [Row { cells: [1, 2, 3, 4] }, Row { cells: [1, 2, 3, 4] },
                        Row { cells: [1, 2, 3, 4] }, Row { cells: [1, 2, 3, 4] }];
    let view = View { first: rows[0].cells };
    fn peek() -> i64 { view.first.len }
    var out: i64[4] = [0, 0, 0, 0];
    par for &row, k in rows { row.cells.clear(); out[k] = peek(); }
    print(out);
}
