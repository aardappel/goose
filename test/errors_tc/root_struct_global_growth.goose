// error: still under construction
root struct State { first: u8[][>..], second: u8[][>..] }
var state: State;
fn extra() -> u8[] { state.first.push("a"); "b" }
fn main() { state.first.push(extra()); }
