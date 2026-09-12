fn main() {
    var rows: i64[2][>..<] = [[1, 2]];
    var other: i64[2] = [1, 2];
    fn replace() -> i64[:] {
        rows.pop();
        rows.push([7, 8]);
        return other[..];
    }
    print(rows[0] == replace());
}
