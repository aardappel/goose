// error: cannot clear reusable pool pool (which cache.bytes may point at)
import std;
reusable[] var pool: u8[>..] = [];
struct Cache { bytes: u8[>..]& }
fn change(cache: Cache&) { cache.bytes .= pool; }
fn clear_cache(cache: Cache&) { change(cache); cache.bytes.clear(); }
fn main() {
    var bytes: u8[>..] = [];
    var cache = Cache { bytes: bytes };
    clear_cache(cache);
}
