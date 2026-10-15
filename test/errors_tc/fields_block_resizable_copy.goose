// error: is not fixed-size and is not copied implicitly
// A block's parameter takes a value (§4.1, §7.6): a string field is passed
// by reference with fields(&v), not copied by fields(v) (§12).
struct Named { name: u8[], n: i64 }
fn main() {
    let v = Named { name: "abc", n: 5 };
    fields(v) { name, f => print(name, " ", f) }
}
