// error: qput in a par for's callee: an iteration's only effect may be its writes to what it owns
// R3 (§4.3), in a function the body calls.
fn send(k: i64) { qput(k); }

fn main() {
    par for k in 3 { send(k); }
}
