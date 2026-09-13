import std;
enum Value { A { items: i64[>..] }, B { n: i64 } }
fn tag(v: Value..&) -> i64 { match v { A => 1, B b => b.n } }
fn main() {
    var a: Value.. = Value.A { items: [1, 2] };
    var b: Value.. = Value.B { n: 7 };
    print(tag(a), " ", tag(b));
    match a { A => print("A"), B b => print(b.n) }
    match b { A => print("A"), B b => print(b.n) }
}
