// error: variant L.A contains itself by value
// A holds B's payload by value, and B an array of A's (§3.4).
enum L { A { x: L.B }, B { y: L.A[] } }
fn main() {
    let l: L.. = L.B { y: [] };
    print(l);
}
