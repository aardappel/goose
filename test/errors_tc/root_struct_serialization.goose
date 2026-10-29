// error: .to_bytes is not available on State
root struct State { a: i64[>..], b: i64[>..] }
fn main() { var state = State { .. }; let image = state.to_bytes(); }
