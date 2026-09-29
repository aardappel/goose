// error: cannot clear ps while rr is still used: a reference to a slice that may point into it was stored there
// s is a byte view of ps's elements, which no u8 slice could otherwise view:
// rr leads to it through s (§5.1, serialization).
struct P { x: i64 }
struct R { r: const (const u8[:])& }
fn main() {
    var ps: P[>..] = [];
    ps.push(P { x: 0x4142434445464748 });
    var s = ps.bytes_of();
    var rr = R { r: s };
    ps.clear();
    ps.push(P { x: 0x5a5a5a5a5a5a5a5a });
    print(rr.r[0]);
}
