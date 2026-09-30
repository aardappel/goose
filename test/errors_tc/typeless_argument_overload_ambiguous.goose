// error: ambiguous call to f: multiple overloads match equally well
// A [] ranks as a coercion wherever it stands among the arguments (§7.7),
// so a generic overload it matches beside a typed argument ties with a
// concrete one at that rank (§7.1), as it does in f(n, []).
fn f<T>(xs: T[], x: T) -> i64 { 1 }
fn f(xs: i64[], x: i64) -> i64 { 2 }
fn main() {
    let n = 5;
    print(f([], n));
}
