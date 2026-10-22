// error: constant pattern 1 has no payload to bind
// A const for constant written as an integer match pattern is its value,
// which has no payload for a binder to take, as a named constant has none
// (match_constant_binder.goose).
fn main() {
    let v = 1;
    match v {
        const for <N> in [1, 2] { N x => print(x), },
        _ => print(0),
    }
}
