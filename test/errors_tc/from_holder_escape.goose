// A long-distance return of a holder may not carry references into the
// frames it unwinds.
struct Holder { value: i64& }
fn inner() -> i64 {
    var local: i64[>..] = [];
    local.push(42);
    return Holder { value: local[0] } from outer;
}
fn outer() -> Holder {
    inner();
    abort("unreachable");
}
fn main() {
    let h = outer();
    print(h.value);
}
