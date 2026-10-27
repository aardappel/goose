// error: cannot clear reusable pool pool (which a.bytes may point at)
import std;
reusable[] var pool: u8[>..] = [];
struct Cache { bytes: u8[>..]& }
fn change_and_clear(a: Cache&, b: Cache&) {
    b.bytes .= pool;
    a.bytes.clear();
}
fn main() {
    var bytes: u8[>..] = [];
    var cache = Cache { bytes: bytes };
    change_and_clear(cache, cache);
}
