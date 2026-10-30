// error: queue elements must be flat
struct Wrapped { view: SharedView }
fn main() { let o, ok = shared_alloc(8); qput(Wrapped { view: shared_view(o, 0, 8) }); }
