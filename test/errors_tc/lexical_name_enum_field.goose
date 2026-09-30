// error: array size N names a type parameter, not a constant
// So do a generic enum's variants' fields (§3.3).
let N = 2;

enum E<N> { A { a: u8[N], b: N }, B }

fn main() {
    let e: E<u8> = E<u8>.A { a: [1, 2], b: 1 };
    print(match e { A => 1, B => 2 });
}
