// error: cannot assign through this path (const, or a read-only instantiation
// A chunk of a const array is a `const i64[:]`, as a slice of it is (§9.5).
fn main() {
    const a: i64[4] = [1, 2, 3, 4];
    let c = a.chunk(0, 2);
    c[0] = 9;
    print(a);
}
