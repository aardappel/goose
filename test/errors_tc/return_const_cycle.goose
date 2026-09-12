recursive fn choose(r: i64&, depth: i64) -> i64& {
    if depth > 0 {
        let x .= choose(r, depth - 1);
        x = 99;
    }
    let c: const i64& = r;
    return c;
}
fn main() { var n = 1; let x .= choose(n, 1); print(x); }
