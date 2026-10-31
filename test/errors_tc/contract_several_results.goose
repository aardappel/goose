// error: ensures on function divmod, which has several results, is not supported yet
// Phase 1 of docs/design/proofs.md names one result only, as `result`.
fn divmod(a: i64, b: i64) -> i64, i64
    ensures result >= 0
{
    return a / b, a % b;
}

fn main() {
    let q, r = divmod(7, 2);
    print(q, r);
}
