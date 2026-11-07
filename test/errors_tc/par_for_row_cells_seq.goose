// error: a par for's iterations each read their element of the sequence
// Each iteration reads its row, while each owns a cell of the first one
// (docs/design/child_threads.md §4.4).
struct Row { cells: i64[4] }
fn main() {
    var rows: Row[4] = [Row { cells: [1, 2, 3, 4] }, Row { cells: [5, 6, 7, 8] },
                        Row { cells: [9, 10, 11, 12] }, Row { cells: [13, 14, 15, 16] }];
    let cells = rows[0].cells[..];
    par for r, k in rows { cells[k] = r.cells[0] + 1; }
    print(rows[0].cells);
}
