// Named field initializers must follow declaration order (§4.2).
struct P { a: int, b: int }
fn main() {
    let p = P { b: 2, a: 1 };
    print(p.a);
}
