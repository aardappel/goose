// error: cannot write through this reference: non-writable provenance
// `&n` of a var of a const type is a const i64& (§9.5): the variable is
// assigned as a whole by its own name, never through a reference to it.
fn main() {
    var n: const i64 = 1;
    n = 2;
    let r = &n;
    r = 3;
    print(n);
}
