// error: native capabilities cannot be serialized
struct Wrapped { view: SharedView }
fn main() { let forged, ok = from_bytes<Wrapped[>..]>(""); }
