// error: cannot call clear_cache, which shrinks bytes while live is still used
import std;
reusable[] var pool: u8[>..] = [];
struct Cache { bytes: u8[>..]& }
fn clear_cache(cache: Cache&) { cache.bytes.clear(); }
fn main() {
    var bytes: u8[>..] = [7];
    var cache = Cache { bytes: bytes };
    let live = bytes[..];
    clear_cache(cache);
    print(live);
}
