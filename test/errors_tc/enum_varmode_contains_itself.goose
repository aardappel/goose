// error: enum L contains itself by value
// In variable mode, too, a payload may refer to its own ADT (§3.4), not
// hold it.
enum L { Nil, Cons { v: i64, next: L.. } }
fn main() {
    let l: L.. = L.Nil;
    print(l);
}
