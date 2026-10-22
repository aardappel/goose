// error: variant type of non-ADT type i64
// error: constant 300 does not fit u8
// A call's candidate is tried with the callee's own type parameters kept
// from enclosing bindings, and with nothing recorded of how literals
// adapt; an error in the trial leaves both as they were for the roots
// after it (docs/design/language_server.md §5 D2): id's T is still found,
// and narrow's literal argument is still checked against the u8 its
// parameter becomes (§7.7).
enum E { A { n: i64 }, B }
fn f<T>(a: T, x: T.A) -> i64 { 0 }
let G = f(1, 2);
fn narrow<U>(v: U) -> u8 { v }
let N = narrow(300);
fn id<T>(x: T) -> T { let y: T = x; y }
fn main() { print(id(5), " ", N); }
