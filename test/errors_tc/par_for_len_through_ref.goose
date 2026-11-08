// error: first is or may point into storage partitioned by this par for
// The length of a row's cells lies in the row, which an iteration owns and
// may clear: reading it through a reference from outside the body reads
// another iteration's element (docs/design/child_threads.md §4.4).
struct Row { cells: i64[..4] }
fn main() {
    var rows: Row[4] = [Row { cells: [1, 2, 3, 4] }, Row { cells: [1, 2, 3, 4] },
                        Row { cells: [1, 2, 3, 4] }, Row { cells: [1, 2, 3, 4] }];
    let first = &rows[0].cells;
    var out: i64[4] = [0, 0, 0, 0];
    par for &row, k in rows { row.cells.clear(); out[k] = first.len; }
    print(out);
}
