// A loop over the limited array an element holds reads the element's view
// once where its body cannot store into an element of that array or
// overwrite the array: a grow or shrink of an array of another type, or a
// growth of that array itself into fresh slots, leaves the element where it
// lies, as it was. Where the body stores into the element, the loop reads it
// again and ends early.
import std;

struct Graph { adj: i64[..4][>..] }
var seen: i64[>..] = [];

recursive fn trim(xs: i64[>..]&) {
    if xs.len > 2 { xs.pop_n(2); }
}

fn walk_other(g: Graph&, v: i64) -> i64 {
    var t = 0;
    for x in g.adj[v] {
        t += x;
        seen.push(x);
    }
    return t;
}

fn walk_callee(g: Graph&, v: i64) -> i64 {
    var t = 0;
    for x in g.adj[v] {
        t += x * 10;
        seen.push(x);
        trim(seen);
    }
    return t;
}

fn walk_own(g: Graph&, v: i64) -> i64 {
    var t = 0;
    for x in g.adj[v] {
        t += x * 100;
        g.adj.push([x, x]);
    }
    return t;
}

fn walk_store(g: Graph&, v: i64) -> i64 {
    var t = 0;
    for x in g.adj[v] {
        t += x;
        g.adj[v] = [x];
    }
    return t;
}

fn walk_pop(g: Graph&, v: i64) -> i64 {
    var t = 0;
    for x in g.adj[v] {
        t += x;
        g.adj[v].pop();
    }
    return t;
}

fn main() {
    var g = Graph { adj: [] };
    g.adj.push([1, 2, 3, 4]);
    g.adj.push([5, 6, 7]);
    print(walk_other(g, 0), " ", walk_callee(g, 1), " ", walk_own(g, 0), " ", g.adj.len, " ",
          seen.len);
    print(walk_store(g, 0), " ", walk_pop(g, 1), " ", g.adj[0].len, " ", g.adj[1].len);
}
