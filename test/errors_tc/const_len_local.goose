// error: array size table names a local variable, not a constant
// A `.len` in a size reads the global of its name (§3.3), which a local of
// that name hides.
let table = [1, 2, 3];

fn main() {
    let table = [1];
    let a: i64[table.len] = [1, 2, 3];
    print(a, table);
}
