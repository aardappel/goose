// error: safe native capabilities have no raw C ABI
extern fn foreign(v: SharedView) = "fake_shared_api";
fn main() { let o, ok = shared_alloc(8); foreign(shared_view(o, 0, 8)); }
