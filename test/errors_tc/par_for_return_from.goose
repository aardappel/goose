// error: return ... from out of a par for
// R4 (§4.3): a long-distance return out of the body, from a nested function
// the body calls.
fn search(xs: const i64[:]) -> i64 {
    fn found(x: i64) { if x > 2 { return x from search; } }
    par for x in xs { found(x); }
    return -1;
}

fn main() { print(search([1, 2, 3])); }
