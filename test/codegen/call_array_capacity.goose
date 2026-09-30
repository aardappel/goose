// A slice returned as a static-capacity limited array has its capacity
// checked (§4.2) before the result constructs the variable array the call
// is passed as, also where -O2 inlines the call and builds the value in
// the argument's slot: the second call aborts at every level.
var h: u8[>..] = [];
fn clip(s: u8[:]) -> u8[..4] { return s; }
fn take(x: u8[]) -> i64 { return x.len; }
fn main() {
    h.append("abc");
    print(take(clip(h[..])));
    h.append("de");
    print(take(clip(h[..])));
}
