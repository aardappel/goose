// A `u16 in pool` offset spans the pool, not the distance between the two
// ends, so it covers the first 65,535 bytes of it and a store past that
// aborts (§3.9, §9.3). The check is emitted wherever the stack reservation
// can exceed the width, which the default GS_STACK_RESERVE of 256 MB does.

struct UNode { v: i64, link: UNode&<u16 in upool>? }

var upool: UNode[>..] = [];

fn main_inpool_u16() {
    upool.push(UNode { v: 1 });
    // 10 bytes a node, so node 6553 starts at offset 65,530 and node 6554 at
    // 65,540 — past what a u16 offset reaches.
    for i in 6553 { upool.push(UNode { v: i }); }
    print(upool.len);                    // 6554
    upool[0].link .= upool[6553];       // The last offset that fits.
    let last = upool[0].link;
    assert(last);
    print(last.v);                       // 6552
    upool.push(UNode { v: 2, link: upool[0] });
    print(upool.len);                    // 6555
    upool[0].link .= upool[6554];       // Aborts: the offset needs 17 bits.
    print(0);                            // Not reached.
}

fn main() { main_inpool_u16(); }
