// error: the match arm's value is a slice rooted at a, which does not outlive it
// A by-value binder is a copy local to its arm.
enum Slot { Full { vals: i64[3] }, Empty }
fn mk(k: i64) -> Slot { if k > 0 { Slot.Full { [k, k + 1, k + 2] } } else { Slot.Empty } }
fn second(s: i64[:]) -> i64 { s[1] }
fn main() {
    var xs: i64[>..] = [1, 2, 3];
    print(second(match mk(xs[0]) { Full a => a.vals[..], Empty => xs[..] }));
}
