// runtime-debug: builder publication and freelist ordering match reusable allocation.
import slot_pool;

struct PN { key: i64, link: PN&<u32 in P.items> }
var P = slot_pool<PN> {};

fn peek() -> i64 {
    let l .= P.items[P.items.len - 1].link;
    print("in pool: ", P.items.index_of(l), " ", l.key);
    return 2;
}

fn try_add(k: i64) -> bool {
    P.alloc_with() { PN { key: if k < 0 { return false from try_add } else { k }, link: self } };
    return true;
}

fn free_second() -> i64 {
    P.free(1);
    return 7;
}

struct SN { key: i64, link: SN&<u32> }
var S = slot_pool<SN> {};

fn speek() -> i64 {
    let l .= S.items[S.items.len - 1].link;
    print("self-relative: ", S.items.index_of(l), " ", l.key);
    return 2;
}

fn lpeek(pool: SN[>..]&) -> i64 {
    let l .= pool[pool.len - 1].link;
    print("local: ", pool.index_of(l), " ", l.key);
    return 2;
}

// At -O0 the local pool below starts where this array was, so a slot of it
// read before its element is written would hold an offset far outside it.
fn junk() -> i64 {
    var j: i64[>..] = [];
    for i in 8 { j.push(0x7fff00007fff0000); }
    return j[7] & 1;
}

fn local_pool() {
    var pool = slot_pool<SN> {};
    pool.alloc_with() { SN { key: 1, link: self } };
    pool.alloc_with() { SN { key: lpeek(pool.items), link: self } };       // local: 0 1
    print(pool.items.len, " ", pool.items[1].key, " ", pool.items[1].link.key);    // 2 2 2
}

fn main() {
    P.alloc_with() { PN { key: 1, link: self } };
    P.alloc_with() { PN { key: peek(), link: self } };               // in pool: 0 1
    print(P.items.len, " ", P.items[1].key, " ", P.items[1].link.key);             // 2 2 2
    print(try_add(-1), " ", P.items.len);                              // false 2
    for n in P.items { print(n.link.key); }                            // 1, 2
    P.free(0);
    let allocated .= P.alloc_with() { PN { key: free_second(), link: self } };
    let i = P.items.index_of(allocated);
    print(i, " ", P.items[i].key, " ", P.items[i].link.key, " ", P.items.len);     // 0 7 7 2
    print(P.items.index_of(P.alloc_with() { PN { key: 8, link: self } }), " ", P.items.len); // 1 2
    S.alloc_with() { SN { key: 1, link: self } };
    let r: SN& = S.alloc_with() { SN { key: speek(), link: self } };   // self-relative: 0 1
    print(S.items.len, " ", r.key, " ", r.link.key);                   // 2 2 2
    let z = junk();
    local_pool();
    print(z);                                                    // 0
}
