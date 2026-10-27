// An array literal whose elements are not fixed-size is a T[] where nothing
// names its type (§3.3: only variable and grow-only arrays hold such
// elements), and is built wherever a construction context puts it (§4.2):
// a local or global initializer, a by-value argument of a declared, untyped
// or generic parameter, a returned value, a field, an element of another
// literal, an `if` value, an appended run and a pushed element. Its `.len`
// needs no view. Each one is then read through a slice parameter.
struct Txt { body: u8[] }
struct Box { ts: Txt[] }

fn lens(ts: Txt[:]) -> i64 {
    var n = 0;
    for t in ts { n += t.body.len; }
    n
}
fn owned(ts: Txt[]) -> i64 { lens(ts) }
fn untyped(ts) -> i64 { lens(ts) }
fn generic<T>(x: T) -> i64 { untyped(x) }
fn filled() -> Txt[] { [Txt { body: "abc" }; 3] }

let G = [Txt { body: "global" }, Txt { body: "!" }];

fn built() {
    let local = [Txt { body: "hey" }, Txt { body: "x" }];
    print(lens(local));
    print(lens(G));
    let fill = [Txt { body: "zz" }; 4];
    print(lens(fill));
    print(owned([Txt { body: "four" }, Txt { body: "five!" }]));
    print(untyped([Txt { body: "ab" }, Txt { body: "c" }]));
    print(generic([Txt { body: "abcdef" }]));
    print(lens(filled()));
    let b = Box { ts: [Txt { body: "in" }, Txt { body: "box" }] };
    print(lens(b.ts));
    let nested = [[Txt { body: "a" }], [Txt { body: "bb" }, Txt { body: "ccc" }]];
    var n = 0;
    for row in nested { n += lens(row); }
    print(n);
    let s: u8[] = "copied";
    let copies = [Txt { body: copy(s) }, Txt { body: copy(s) }];
    print(lens(copies));
    print(owned([Txt { body: copy(s) }]));
    let wide = local.len > 1;
    let pick = if wide { [Txt { body: "then" }] } else { [Txt { body: "else!" }] };
    print(lens(pick));
    print([Txt { body: "p" }, Txt { body: "q" }, Txt { body: "r" }].len);
    var grown: Txt[>..] = [];
    grown.append([Txt { body: "12" }, Txt { body: "345" }]);
    grown.push(Txt { body: "6" });
    print(lens(grown));
    var rows: Txt[][>..] = [];
    rows.push([Txt { body: "row" }]);
    rows.push([Txt { body: "row" }, Txt { body: "two" }]);
    var m = 0;
    for r in rows { m += lens(r); }
    print(m);
}

// A literal of variable-size elements is a T[], and == and != compare it with
// any array or slice of its element type (§4.5): with a T[] directly, and as
// a slice with a grow-only array, a slice parameter or a generic one, on
// either side, inside && and ||, in a loop condition, and with copies for
// elements. The comparison views the literal only while it runs.
fn same(ts: Txt[:]) -> bool { return ts == [Txt { body: "a" }, Txt { body: "bc" }]; }
fn has<T>(xs: T[:], x: T) -> bool { return xs == [copy(x)] || [copy(x), copy(x)] == xs; }

fn compared() {
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

fn main() {
    built();
    compared();
}
