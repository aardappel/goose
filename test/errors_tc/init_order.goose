// error: field initializers must follow declaration order: a comes before b
// error: supplied fields in declaration order: a, b
// error: fields are evaluated and constructed front-to-back
// Named field initializers must follow declaration order (§4.2).
struct P { a: i64, b: i64 }
fn main() {
    let p = P { b: 2, a: 1 };
    print(p.a);
}
