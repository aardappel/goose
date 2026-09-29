// What a root that only bounds a reference leads to may hold a grow-shrink
// array, whose elements are never stored (§5.2). Each store below stays
// legal beside such an array: the parameter itself, a reference to the whole
// array, here or passed on, a reference read out of a field, bound to a
// variable or passed on, a view of storage no grow-shrink array holds, and a
// u8 view a slot held, which the rule let in only where it covered none.
struct Cell { v: i64 }
struct Other { w: i64 }
struct Top { a: Cell[>..<]&, r: Cell&, b: Other& }
struct Only { a: Cell[>..<]& }
struct Ctl { x: i64, y: i64 }
struct Ctx { text: u8[>..]&, ctls: Ctl[>..<]& }
struct CV { code: const u8[:] }
struct Keep { v: const u8[:] }
struct TopG { a: Cell[>..]& }

fn keepr(c: Cell&, loc: (Cell&)[>..]&) { loc.push(c); }
fn keepo(o: Other&, loc: (Other&)[>..]&) { loc.push(o); }
fn keepa(arr: Cell[>..<]&, arrs: (Cell[>..<]&)[>..]&) { arrs.push(arr); }
fn look(arr: Cell[>..<]&) -> i64 { arr[0].v }

fn by_ref(t: Top&) {
    var loc: (Cell&)[>..] = [];
    var los: (Other&)[>..] = [];
    var tops: (Top&)[>..] = [];
    var arrs: (Cell[>..<]&)[>..] = [];
    loc.push(t.r);
    keepr(t.r, loc);
    let x: Cell& = t.r;
    loc.push(x);
    los.push(t.b);
    keepo(t.b, los);
    tops.push(t);
    arrs.push(t.a);
    keepa(t.a, arrs);
    print(loc[0].v, loc[1].v, loc[2].v, los[0].w, los[1].w, tops[0].a[0].v, arrs[0][0].v,
          arrs[1][0].v, look(t.a));
}

fn by_value(t: Top) {
    var loc: (Cell&)[>..] = [];
    loc.push(t.r);
    print(loc[0].v, look(t.a));
}

fn by_value_only(t: Only) {
    var copies: Only[>..] = [];
    var arrs: (Cell[>..<]&)[>..] = [];
    copies.push(t);
    arrs.push(t.a);
    keepa(t.a, arrs);
    print(copies[0].a[0].v, arrs[0][0].v, arrs[1][0].v);
}

// ctx leads to a grow-shrink array of Ctls, which holds no u8: text saved
// in its grow-only text is no grow-shrink array's. A recursive call's u8
// views may be byte views, but a CV holds only what was stored in it.
fn save(ctx: Ctx&, s: const u8[:]) -> const u8[:] {
    let start = ctx.text.len;
    ctx.text.append(s);
    ctx.text[start..]
}
fn code_of(ctx: Ctx&, v: CV) -> const u8[:] {
    if v.code.len > 3 { return v.code; }
    save(ctx, "x")
}
recursive fn expr(ctx: Ctx&, n: i64) -> CV {
    if n == 0 { return CV { code: save(ctx, "leaf") }; }
    let original = expr(ctx, n - 1);
    CV { code: code_of(ctx, original) }
}

// Views of a grow-only array are stored, and checked where it shrinks
// (§5.1).
fn grow_only(t: TopG&) {
    var keep: Keep[>..] = [];
    keep.push(Keep { v: t.a.bytes_of() });
    var loc: (Cell&)[>..] = [];
    loc.push(t.a[0]);
    print(keep[0].v.len, loc[0].v);
}

fn main() {
    var mine: Cell[>..<] = [Cell { v: 1 }];
    var solo = Cell { v: 2 };
    var other = Other { w: 3 };
    var top = Top { a: mine, r: solo, b: other };
    by_ref(top);
    by_value(top);
    var onlys: Only[>..] = [];
    onlys.push(Only { a: mine });
    by_value_only(onlys[0]);
    var text: u8[>..] = [];
    var ctls: Ctl[>..<] = [];
    var ctx = Ctx { text: text, ctls: ctls };
    print(expr(ctx, 3).code);
    var grown: Cell[>..] = [Cell { v: 4 }];
    var tgs: TopG[>..] = [];
    tgs.push(TopG { a: grown });
    grow_only(tgs[0]);
}
