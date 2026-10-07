// error: field initializers must follow declaration order: a comes before b
// error: supplied fields in declaration order: a, b
// error: fields are evaluated and constructed front-to-back
// A potentially effectful initializer cannot cross even a literal (§4.2).
struct P { a: i64, b: i64 }
var state = 0;
fn effect() -> i64 { state++; state }
fn main() {
    let p = P { b: effect(), a: 1 };
    print(p.a);
}
