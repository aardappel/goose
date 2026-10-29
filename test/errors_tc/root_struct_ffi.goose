// error: cannot cross to C: only fixed-size values cross by value
root struct State { n: i64 }
extern fn inspect(state: State&);
fn main() {}
