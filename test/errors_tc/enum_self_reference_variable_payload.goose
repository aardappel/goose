// error: enum L has non-fixed-size payloads and can only be used in variable mode (L..)
// A payload may refer to its own ADT (§3.4), but a fixed-mode L needs all
// of L's payloads fixed-size (§3.5), and S's is not: the link to a
// variable-mode list is L..?.
enum L { Nil, Cons { v: i64, next: L? }, S { s: u8[] } }
fn main() {
    let l: L.. = L.Nil;
    print(l);
}
