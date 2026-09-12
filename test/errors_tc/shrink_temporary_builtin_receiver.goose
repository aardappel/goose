fn main() {
    var rows: i64[..3][>..<] = [[1]];
    fn replace() -> i64 { rows.pop(); rows.push([8]); return 9; }
    rows[0].push(replace());
}
