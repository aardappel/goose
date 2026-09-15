// runtime-debug: a slice the checker cannot place in its pool is checked when the call runs.
struct Holder { s: i64[:] }

fn give_back(pool: i64[>..]&, s: i64[:]) { pool.free_slice(s); }

fn main() {
    reusable[] var pool: i64[>..] = [];
    var other: i64[>..] = [1, 2, 3];
    give_back(pool, pool.alloc_slice(2));   // the pool's own, and known to be
    print(pool.len);
    // Read out of h, the run could be other's as far as the checker knows: it
    // is checked, and it is the pool's.
    var h = Holder { s: pool.alloc_slice(2) };
    give_back(pool, h.s);
    print(pool.len, " ", other.len);
    give_back(pool, other[0..2]);           // checked, and not the pool's
    print("unreachable");
}
