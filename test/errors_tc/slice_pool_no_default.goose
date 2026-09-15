// error: .alloc_slice fills the elements it adds with default values
// error: field r has no declared default
// A slice pool's new elements are default values, which a type holding a
// non-optional reference has not got (§4.2, §5.4).
struct Holder { r: i64& }
fn main() {
    reusable[] var pool: Holder[>..] = [];
    let s = pool.alloc_slice(1);
    print(s.len);
}
