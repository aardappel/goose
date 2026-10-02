// error: the block's value is a slice rooted at a temporary, which does not outlive it (§9.2): a temporary lasts until the end of its statement, or of the block whose final expression made it; make the value in a variable before the statement, or, where every branch makes a new array of one type, slice the whole construct
// Each branch's string is a temporary of that branch, which its view as the
// slice argument would outlive; the message names the two ways around it
// (lifetimes/branch_array_whole_slices).
import std;
fn take(s: u8[:]) { print(s); }
fn main() {
    let c = true;
    let n = 3;
    take(if c { str("a", n) } else { str("b") });
}
