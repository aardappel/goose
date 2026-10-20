// error: const for source requires exactly 2 binding(s)
enum E { A, B }
fn main() { const for <V> in variants<E> {} }
