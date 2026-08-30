// ADT matches are exhaustive (§8.1).
enum E { A, B, C }
fn main() {
    let e: E = E.A;
    let v = match e { A => 1, B => 2 };
    print(v);
}
