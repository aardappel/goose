// error: cannot consume b while pb is still used
// The consume of one handle frees the views read from it, and leaves the
// other handle's views alone.
import slot_pool;
struct Buf { n: i64 }
var blobs = slot_pool<Buf> {};
fn main() {
    let b: own Buf& = blobs.take(Buf { n: 1 });
    let g: own Buf& = blobs.take(Buf { n: 2 });
    let pb: Buf& = b;
    let pg: Buf& = g;
    print(pg.n);
    blobs.release(b) { };
    print(pb.n);
    blobs.release(g) { };
    print(pg.n);
}
