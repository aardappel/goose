// error: field a of struct L cannot be a zero-length array: u8[..0]
// A limited array of capacity 0 has no element slots either (§3.4).
struct L { a: u8[..0], b: i64 }

fn main() {
    var ls: L[>..] = [];
    ls.push(L { a: [], b: 3 });
    print(ls[0].b);
}
