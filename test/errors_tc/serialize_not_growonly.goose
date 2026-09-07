// from_bytes builds an array whose contents are an element run plus a count.
// A limited array would need the count to match a capacity no image carries.
fn load(bytes: u8[:]) -> i64 {
    var a, ok = from_bytes<i64[..8]>(bytes);
    return if ok { a.len } else { -1 };
}

fn main() { print(load("")); }
