// error: type parameter N is bound to the type i64, not a value
// A typed for loop's end is probed for a literal one past its binder's
// largest value (§6.5) only where no type parameter hides the global N,
// whose -N a u8 could not compute (§11.1).
let N: u8 = 3;

fn f<N>(x: N) -> i64 {
    var s = 0;
    for i: i8 in -9..-N { s += 1; }
    s
}

fn main() { print(f(1)); }
