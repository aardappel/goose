// A literal of variable-size elements is a T[], and == and != compare it with
// any array or slice of its element type (§4.5): with a T[] directly, and as
// a slice with a grow-only array, a slice parameter or a generic one, on
// either side, inside && and ||, in a loop condition, and with copies for
// elements. The comparison views the literal only while it runs.
struct Txt { body: u8[] }

fn same(ts: Txt[:]) -> bool { return ts == [Txt { body: "a" }, Txt { body: "bc" }]; }
fn has<T>(xs: T[:], x: T) -> bool { return xs == [copy(x)] || [copy(x), copy(x)] == xs; }

fn main() {
    let ts = [Txt { body: "hey" }, Txt { body: "yo" }];
    var r = 0;
    for t in ts { r += t.body.len; }
    print(r);
    print(ts == [Txt { body: "hey" }, Txt { body: "yo" }], " ", ts != [Txt { body: "hey" }], " ",
          [Txt { body: "x" }] == [Txt { body: "x" }, Txt { body: "x" }]);
    var g: Txt[>..] = [];
    g.push(Txt { body: "a" });
    print(g == [Txt { body: "a" }], " ", [Txt { body: "a" }] == g, " ",
          g != [Txt { body: "a" }], " ", has(g, Txt { body: "a" }), " ",
          has(g, Txt { body: "b" }));
    g.push(Txt { body: "bc" });
    print(same(g), " ", same(ts), " ", g[..] == [Txt { body: "a" }, Txt { body: "bc" }], " ",
          g.len == 2 && [Txt { body: "a" }, Txt { body: "bc" }] == g);
    let a: u8[] = "hi";
    let b: u8[] = "yo";
    var w: u8[][>..] = [];
    w.push(copy(a));
    while w != [copy(a), copy(b), copy(a)] && w.len < 5 {
        if w.len == 1 { w.push(copy(b)); } else { w.push(copy(a)); }
    }
    print(w, " ", w == [copy(a), copy(b), copy(a)]);
}
