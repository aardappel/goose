// error: variant L.Cons contains itself by value
// A variant type is its payload (§3.5), so holding one by value in that
// payload makes it part of itself (§3.4).
enum L { Nil, Cons { v: i64, next: L.Cons } }
fn main() {
    let l = L.Nil;
    print(l);
}
