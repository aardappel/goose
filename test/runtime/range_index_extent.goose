// runtime-debug: sharing a start address does not prove full containment.
fn main() {
    var rows: u8[2][2] = [[1, 2], [3, 4]];
    print(rows[0].range_index(rows.bytes_of()));
}
