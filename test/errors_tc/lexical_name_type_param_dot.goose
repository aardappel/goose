// error: type parameter E is bound to the type E, not a value
// In f, E is the type parameter, not the enum of its name (§11.1).
enum E { A, B }

fn f<E>(x: E) -> E { E.A }

fn main() { print(f(E.B) == E.A); }
