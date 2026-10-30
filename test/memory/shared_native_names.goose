struct local::SharedOwner { value: i64 }
struct local::SharedView { value: i64 }
fn local::get() -> i64 { let x = SharedOwner { value: 42 }; x.value }
fn local::native_size(v: ::SharedView) -> u64 { shared_size(v) }
fn ::identity<SharedOwner>(x: SharedOwner) -> SharedOwner { x }
fn ::main() { let o, ok = shared_alloc(8); assert(ok); assert(local::native_size(shared_view(o, 0, 8)) == 8); assert(shared_close(o)); assert(identity(7) == 7); assert(local::get() == 42); print("native names ok"); }
