// A field no index or rebinding on the way can move is stored to as a C
// member store (StableDest), which the C compiler's type-based alias
// analysis keeps apart from other structs' fields: run_tests.py reads the
// -O1 C of store_links. The others take the destination's address first.

struct Node { e: f64, w: f64 }
struct Link { w: f64, prev: f64 }

fn store_links(n: Node&, links: Link[:], rate: f64) {
    for &l in links {
        let t = l.w;
        l.w += rate * n.e + (l.w - l.prev);
        l.prev = t;
    }
    n.w = rate;
}

// An index on the way.
fn store_indexed(links: Link[:], i: i64) { links[i].w = 1.5; }

// A reference that can be rebound.
fn store_rebindable(a: Link&, b: Link&) {
    var r .= a;
    r.w = { r .= b; 2.5 };
}

fn main() {
    var n = Node { e: 2.0, w: 0.0 };
    var links: Link[3] = [Link { w: 1.0, prev: 0.5 }, Link { w: 2.0, prev: 2.0 },
                          Link { w: -1.0, prev: 0.0 }];
    store_links(n, links, 0.25);
    for l in links { print(l.w, " ", l.prev); }
    store_links(n, links[1..], 0.5);
    for l in links { print(l.w, " ", l.prev); }
    print(n.w);
    store_indexed(links, 1);
    var other = Link { w: 0.0, prev: 0.0 };
    store_rebindable(links[0], other);
    print(links[0].w, " ", links[1].w, " ", other.w);
}
