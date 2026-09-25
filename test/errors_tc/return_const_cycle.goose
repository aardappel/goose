// error: cannot write through this reference: non-writable provenance
// A cycle's result is read-only where any of its returns is (§9.5), so the
// write through x, checked once the cycle's returns are known, is the error.
recursive fn choose(r: i64&, depth: i64) -> i64& {
    if depth > 0 {
        let x .= choose(r, depth - 1);
        x = 99;
    }
    let c: const i64& = r;
    return c;
}
fn main() { var n = 1; let x .= choose(n, 1); print(x); }
