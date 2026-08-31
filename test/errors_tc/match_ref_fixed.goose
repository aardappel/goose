// &-binders exist for variable-mode payloads only: a fixed-mode value may be
// replaced in place, so references into its payload are illegal (§3.5, §8.1).
enum E { A { v: i64 }, B }
fn main() {
    var e: E = E.A { v: 1 };
    let x = match e { A &a => a.v, B => 0 };
    print(x);
}
