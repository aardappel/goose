// error: struct literal mixes named and positional initializers
struct X { a: i64, b: i64 }
fn main() {
    let x = X { a: 1, 2 };
    print(x.a);
}
