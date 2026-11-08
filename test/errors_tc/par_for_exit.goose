// error: exit in a par for's callee
// R3 (§4.3): the program's exit would happen at a moment the schedule chose.
fn stop(k: i64) { if k == 2 { exit(3); } }

fn main() {
    par for k in 4 { stop(k); }
}
