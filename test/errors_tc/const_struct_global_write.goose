// error: cannot assign through this path (const, or a read-only instantiation
// An annotated const global of a struct type is read-only in every function
// (§9.5), and static data in the generated C.
struct S { n: i64 }
const ORIGIN: S = S { n: 1 };
fn reset() { ORIGIN.n = 0; }
fn main() {
    reset();
    print(ORIGIN.n);
}
