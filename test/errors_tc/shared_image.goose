// error: native capabilities cannot be serialized
fn main() { let v: SharedView[>..] = []; let image = to_bytes(v); }
