// error: break out of a par for: an iteration ends by running to the end of the body or by continue
// R4 (§4.3): no iteration ends the loop early.
fn main() {
    var seen: i64[4] = [0, 0, 0, 0];
    par for k in 4 {
        if k == 2 { break; }
        seen[k] = 1;
    }
    print(seen);
}
