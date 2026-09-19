// runtime-debug: an element allocated at a pool's top joins it only once it is written.
// An allocated literal holding relative references of either form is built
// in its slot (§3.9). At the top of the pool that slot is counted only once
// the literal is complete, so what its initializers call reads the pool as
// it was before the allocation, and an initializer that leaves the function
// leaves the pool as it was. A slot off the freelist is taken before the
// initializers run, so one they free is left for the next allocation.

struct PN { key: i64, link: PN&<u32 in P> }
reusable var P: PN[>..] = [];

fn peek() -> i64 {
    let l .= P[P.len - 1].link;
    print("in pool: ", P.index_of(l), " ", l.key);
    return 2;
}

fn try_add(k: i64) -> bool {
    P.alloc_index(PN { key: if k < 0 { return false } else { k }, link: self });
    return true;
}

fn free_second() -> i64 {
    P.free(1);
    return 7;
}

struct SN { key: i64, link: SN&<u32> }
reusable var S: SN[>..] = [];

fn speek() -> i64 {
    let l .= S[S.len - 1].link;
    print("self-relative: ", S.index_of(l), " ", l.key);
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
    reusable var pool: SN[>..] = [];
    pool.alloc_index(SN { key: 1, link: self });
    pool.alloc_index(SN { key: lpeek(pool), link: self });       // local: 0 1
    print(pool.len, " ", pool[1].key, " ", pool[1].link.key);    // 2 2 2
}

fn main() {
    P.alloc_index(PN { key: 1, link: self });
    P.alloc_index(PN { key: peek(), link: self });               // in pool: 0 1
    print(P.len, " ", P[1].key, " ", P[1].link.key);             // 2 2 2
    print(try_add(-1), " ", P.len);                              // false 2
    for n in P { print(n.link.key); }                            // 1, 2
    P.free(0);
    let i = P.alloc_index(PN { key: free_second(), link: self });
    print(i, " ", P[i].key, " ", P[i].link.key, " ", P.len);     // 0 7 7 2
    print(P.alloc_index(PN { key: 8, link: self }), " ", P.len); // 1 2
    S.alloc_index(SN { key: 1, link: self });
    let r: SN& = S.alloc_ref(SN { key: speek(), link: self });   // self-relative: 0 1
    print(S.len, " ", r.key, " ", r.link.key);                   // 2 2 2
    let z = junk();
    local_pool();
    print(z);                                                    // 0
}
