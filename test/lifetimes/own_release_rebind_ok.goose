// A view variable rebound to another handle stops answering to the old one:
// the consume of one handle leaves the other's views alone.
import slot_pool;
struct Buf { n: i64 }
var blobs = slot_pool<Buf> {};
fn main() {
    let b: own Buf& = blobs.take(Buf { n: 1 });
    let g: own Buf& = blobs.take(Buf { n: 2 });
    var p: Buf& = b;
    p .= g;
    blobs.release(b) { };
    print(p.n);
    blobs.release(g) { };
}
