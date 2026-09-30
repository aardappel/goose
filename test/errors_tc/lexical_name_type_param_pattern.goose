// error: match pattern N names a type parameter, not a constant
// A match pattern's name resolves as any other does (§8.1, §11.1).
let N = 4;

fn f<N>(x: N) -> i64 {
    match 4 { N => 1, _ => 2 }
}

fn main() { print(f(1)); }
