// error: which may be views of one array from different starts
// The iterations own the rows, and the cells of the first row through a
// slice of them: elements of two types, one of which holds the other, may
// overlap (docs/design/child_threads.md §4.4).
struct Row { cells: i64[4] }
fn main() {
    var rows: Row[4] = [Row { cells: [1, 2, 3, 4] }, Row { cells: [5, 6, 7, 8] },
                        Row { cells: [9, 10, 11, 12] }, Row { cells: [13, 14, 15, 16] }];
    let cells = rows[0].cells[..];
    par for k in 4 {
        rows[k] = Row { cells: [k, k, k, k] };
        cells[k] = 100 + k;
    }
    print(rows[0].cells);
}
