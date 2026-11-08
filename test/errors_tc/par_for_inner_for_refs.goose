// error: cannot assign through this path (const, or a read-only instantiation, §9.5)
// R1 (§4.3): a plain for in a par for's body binds the references an array
// from outside the body holds read-only, whatever the type of their slots:
// every iteration's write through one would be to the same variable.
fn main() {
    var total = 0;
    let refs: i64&[1] = [total];
    par for k in 1000 { for r in refs { r += 1; } }
    print(total);
}
