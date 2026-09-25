// error: .index_of takes a reference to an element of i64[4], got i64
// An if whose other branch never produces is a copy of its value all the
// same, not the element its branch names (§9.2).
var rows: i64[4][2] = [[1, 2, 3, 4], [5, 6, 7, 8]];
fn main() {
    let c = rows[0][0] > 0;
    print(rows[0].index_of(if c { rows[0][1] } else { abort("no") }));
}
