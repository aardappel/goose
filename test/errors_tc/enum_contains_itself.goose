// error: enum L contains itself by value
// A payload may refer to its own ADT (§3.4), not hold it: an L would be
// part of itself.
enum L { Nil, Cons { v: i64, next: L } }
fn main() {
    let l = L.Nil;
    print(l);
}
