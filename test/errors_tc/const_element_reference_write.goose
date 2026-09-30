// error: cannot assign through this path (const, or a read-only instantiation
// An element of a const array type is assigned as a whole, and read-only
// through `&` of it, as a field of one is (§9.5).
fn main() {
    var rows: (const i64[3])[2] = [[1, 2, 3], [4, 5, 6]];
    rows[1] = [7, 8, 9];
    let r = &rows[0];
    r[0] = 9;
    print(rows);
}
