// A relative-reference store whose offset does not fit the declared width
// is a runtime abort (§3.9, §9.3). The check is emitted whenever a data
// stack's reservation exceeds the width's signed range, which the default
// GS_STACK_RESERVE of 256 MB does for every width below `u64`.

struct RNode { v: i64, link: RNode&<u16>? }

fn main_reloff_u16() {
    var pool: RNode[>..] = [];
    let first: RNode& = pool.push(RNode { v: 1 });
    // 10 bytes a node, so the last one sits 39,990 bytes past the first —
    // past the 32,767 a u16 offset reaches.
    for i in 3999 { pool.push(RNode { v: i }); }
    print(pool.len);                     // 4000
    let last: RNode& = pool[3999];
    print(last.v);                       // 3998
    last.link .= first;                  // Aborts: the offset needs 17 bits.
    print(0);                            // Not reached.
}

fn main() { main_reloff_u16(); }
