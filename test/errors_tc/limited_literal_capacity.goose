// A literal constructing a limited array of static capacity must fit it
// (§4.2): three elements into two slots.
fn main() {
    var small: u8[..2] = [1, 2, 3];
    print(small.len);
}
