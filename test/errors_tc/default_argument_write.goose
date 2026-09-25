// error: cannot assign through this path (const, or a read-only instantiation
// default<T>() fills an array in a temporary, as a literal is built, which a
// callee may not write through (§9.2, §9.5).
fn zero(s: i64[:]) { s[0] = 0; }
fn main() {
    zero(default<i64[3]>());
    print(1);
}
