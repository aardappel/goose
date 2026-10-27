// error: cannot clear reusable pool pool (which cache.bytes may point at)
import std;
reusable[] var pool: u8[>..] = [];
struct Cache { bytes: u8[>..]& }
fn clear_cache(cache: Cache&) {
    for i in 2 {
        cache.bytes.clear();
        cache.bytes .= pool;
    }
}
fn main() {
    var bytes: u8[>..] = [];
    var cache = Cache { bytes: bytes };
    clear_cache(cache);
}
