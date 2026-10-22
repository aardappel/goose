// A limited array grows into slots that a reference kept across a shrink may
// still name, and a pool allocates slots it freed: a reference into such a
// slot, or a loop walking the element there, measures what is stored there
// now. Each `with` below is long enough to pass the kept checks; the last
// call's is not.
import std;

fn limited_rows(with: i64[..4]) -> i64 {
    var adj: i64[..4][..3] = [[1, 2, 3, 4], [5, 6, 7, 8], [9, 10, 11, 12]];
    let r = &adj[2];
    var t = 0;
    if r.len > 3 {
        t = r[3];                       // bce:elide
        adj.pop();
        t += r[3];                      // bce:elide (a pop leaves the slot as it was)
        adj.push(with);
        t += r[3];                      // bce:keep (adj[2] is `with` now)
    }
    return t;
}

fn limited_resize(with: i64[..4]) -> i64 {
    var adj: i64[..4][..3] = [[1, 2, 3, 4], [5, 6, 7, 8], [9, 10, 11, 12]];
    let r = &adj[2];
    var t = 0;
    if r.len > 3 {
        adj.resize(1);
        adj.resize(3, with);
        t = r[3];                       // bce:keep (adj[2] is `with` now)
    }
    return t;
}

fn slice_rows(with: i64[:]) -> i64 {
    var big: i64[>..] = [1, 2, 3, 4, 5];
    var ss: i64[:][..3] = [big[..], big[..], big[..]];
    let r = &ss[2];
    var t = 0;
    if r.len > 3 {
        ss.pop();
        ss.append([with]);
        t = r[3];                       // bce:keep (ss[2] is `with` now)
    }
    return t;
}

fn pool_rows(with: i64[..4]) -> i64 {
    reusable var pool: i64[..4][>..] = [];
    pool.push([1, 2, 3, 4]);
    pool.push([5, 6, 7, 8]);
    let r = &pool[0];
    var t = 0;
    if r.len > 3 {
        pool.free(0);
        t = r[3];                       // bce:elide (freeing leaves the slot as it was)
        pool.alloc_ref(with);
        t += r[3];                      // bce:keep (pool[0] is `with` now)
    }
    return t;
}

// A grow-only array never shrinks while a reference into it lives, so its
// growth fills fresh slots only.
fn grow_only_rows(with: i64[:]) -> i64 {
    var big: i64[>..] = [1, 2, 3, 4, 5];
    var ss: i64[:][>..] = [big[..]];
    let r = &ss[0];
    var t = 0;
    if r.len > 3 {
        ss.push(with);
        t = r[3];                       // bce:elide
    }
    return t;
}

// A loop walks the element it names in place, so it ends early once the
// element is refilled with fewer values.
fn walk_limited(adj: i64[..4][..3]&, with: i64[..4]) -> i64 {
    var t = 0;
    for x in adj[2] {
        t += x;
        adj.pop();
        adj.push(with);
    }
    return t;
}

fn walk_slices(ss: i64[:][..3]&, with: i64[:]) -> i64 {
    var t = 0;
    for x in ss[2] {
        t += x;
        ss.pop();
        ss.push(with);
    }
    return t;
}

fn main() {
    var long: i64[>..] = [10, 20, 30, 40];
    var short: i64[>..] = [100];
    print(limited_rows([10, 20, 30, 40]), " ", limited_resize([10, 20, 30, 40]), " ",
          slice_rows(long[..]), " ", pool_rows([10, 20, 30, 40]), " ", grow_only_rows(short[..]));
    var adj: i64[..4][..3] = [[1, 2, 3, 4], [5, 6, 7, 8], [9, 10, 11, 12]];
    var big: i64[>..] = [1, 2, 3, 4, 5];
    var ss: i64[:][..3] = [big[..], big[..], big[..]];
    print(walk_limited(adj, [100]), " ", walk_slices(ss, short[..]));
    print(limited_rows([9]));
}
