// runtime-debug: a `for` over a slice or limited array that an element of an
// array holds loads it again on every iteration (§6.5): a body that stores
// another slice into that element, grows it, or calls a function that does
// either moves the walk on with it, at the next index. A body that writes
// only other storage keeps walking what it started with, which the compiler
// may then read once.
import std;

struct Graph { adj: (i64[:])[>..] }
var gadj: (i64[:])[>..] = [];
var big: i64[>..] = [];
var one: i64[>..] = [];

fn repoint_global(i: i64) { gadj[i] = big[..]; }
fn repoint_param(g: Graph&, i: i64) { g.adj[i] = big[..]; }

// Writes only locals.
fn walk_param(g: Graph&, i: i64) -> i64 {
    var sum = 0;
    var seen: i64[>..] = [];
    for x in g.adj[i] {
        seen.push(x);
        sum += x;
    }
    return sum + seen.len * 100;
}

fn walk_param_repointed(g: Graph&, i: i64) {
    for x, k in g.adj[i] {
        print(x);                               // 1 20 30
        if k == 0 { repoint_param(g, i); }
    }
}

fn greater(a: i64, b: i64) -> bool { a > b }

// The array itself by reference: a heap kept in a local leaves the walk
// alone, a store through the reference does not.
fn walk_ref(adj: (i64[:])[>..]&, i: i64) -> i64 {
    var heap: i64[>..<] = [];
    for x in adj[i] { heap.heap_push(x, greater); }
    var top = 0;
    while heap.len > 0 { top = top * 10 + heap.heap_pop(greater) / 10; }
    return top;
}

fn walk_ref_stored(adj: (i64[:])[>..]&, i: i64) {
    for x, k in adj[i] {
        print(x);                               // 1 20 30
        if k == 0 { adj[i] = big[..]; }
    }
}

fn walk_param_stored(g: Graph&, i: i64, j: i64) {
    for x, k in g.adj[i] {
        print(x);                               // 1 20 30
        if k == 0 { g.adj[j] = big[..]; }
    }
}

fn main() {
    for k in 1..4 { big.push(k * 10); }
    one.push(1);
    var small: i64[>..] = [1];

    var lists: (i64[:])[>..] = [small[..], small[..]];
    for x in lists[1] {
        print(x);                               // 1 20 30
        lists[1] = big[..];
    }
    // Another element changing leaves the walk where it is.
    lists[1] = small[..];
    for x in lists[1] {
        print(x);                               // 1
        lists[0] = big[..];
    }

    gadj.push(one[..]);
    for x, k in gadj[0] {
        print(x);                               // 1 20 30
        if k == 0 { repoint_global(0); }
    }

    var g = Graph { adj: [] };
    g.adj.push(small[..]);
    g.adj.push(big[..]);
    print(walk_param(g, 1));                    // 360
    walk_param_repointed(g, 0);
    g.adj[0] = small[..];
    walk_param_stored(g, 0, 0);
    print(walk_ref(g.adj, 1));                  // 321
    g.adj[0] = small[..];
    walk_ref_stored(g.adj, 0);

    // A pool's run grown in the body moves, and the walk goes with it.
    reusable[] var pool: i64[>..] = [];
    var runs: (i64[:])[>..] = [];
    runs.push(pool.alloc_slice(2));
    runs.push(pool.alloc_slice(1));
    runs[0][0] = 5;
    runs[0][1] = 6;
    for x, k in runs[0] {
        print(x);                               // 5 6 7 8
        if k < 2 {
            let n = runs[0].len;
            runs[0] = pool.realloc_slice(runs[0], n + 1);
            runs[0][n] = 7 + k;
        }
    }
    print(pool.index_of(runs[0][0]), " ", runs[1].len);   // 3 1

    // A limited array in an element, pushed to in the body.
    var rows: (i64[..8])[>..] = [];
    var r: i64[..8] = [1, 2];
    rows.push(r);
    rows.push(r);
    for x in rows[0] {
        print(x);                               // 1 2 3 4
        if x < 3 { rows[0].push(x + 2); }
    }
    var t = 0;
    for x in rows[1] {
        t += x;
        rows[0].push(x);
    }
    print(t, " ", rows[0].len);                 // 3 6
}
