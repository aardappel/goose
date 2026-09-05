// A string literal into a limited array it does not fit (§4.2).
struct Player { name: u8[..4], hp: i32 = 100 }
fn main() {
    var p = Player { name: "gustav" };
    print(p.hp);
}
