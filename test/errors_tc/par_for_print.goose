// error: print in a par for: an iteration's only effect may be its writes to what it owns
// R3 (§4.3): the order of the lines would show the schedule.
fn main() {
    par for k in 3 { print(k); }
}
