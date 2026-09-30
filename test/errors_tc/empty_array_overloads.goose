// error: ambiguous call to f: multiple overloads match equally well
// A [] fits an array parameter and a slice parameter of any element type
// alike, each a coercion (§4.2, §7.1), as [1, 2] fits i64[] and i64[:].
fn f(xs: i64[]) -> i64 { xs.len }
fn f(s: const u8[:]) -> i64 { s.len }
fn main() {
    print(f([]));
}
