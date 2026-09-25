// The value of an if, match, block, loop or bare { } is a copy of the branch
// taken, which a non-fixed variable reaches only through copy() (§4.1). A
// reference parameter instead binds each branch by reference, as it binds
// the variable itself: the callee writes the array the branch names, and a
// reference it returns is rooted where the branches are.
enum Msg { Text { s: u8[] }, Num { n: i64 } }
struct Ctx { args: i64[>..]&, other: i64[>..]& }

fn grow(xs: i64[>..]&, v: i64) -> i64 {
    xs.push(v);
    xs.len
}
fn last<T>(xs: T[>..]&) -> T { xs[xs.len - 1] }
fn first(xs: i64[>..]&) -> i64& { xs[0] }
fn size(t: Msg.Text&) -> i64 { t.s.len }
fn size(n: Msg.Num&) -> i64 { n.n }
fn sized(t: Msg.Text&, xs: i64[>..]&) -> i64 { t.s.len * 100 + xs.len }
fn sized(n: Msg.Num&, xs: i64[>..]&) -> i64 { n.n * 100 + xs.len }
fn stamp(t: Msg.Text&, xs: i64[>..]&) { xs.push(t.s.len); }
fn stamp(n: Msg.Num&, xs: i64[>..]&) { xs.push(n.n); }
fn each<F>(xs: i64[>..]&, ys: i64[>..]&, c: bool) { F(if c { xs } else { ys }); }
fn bump(xs: i64[]&) { xs[0] += 1; }
// Branches giving references bind what those reference.
fn ctxgrow(c: Ctx&, k: bool) -> i64 { grow(if k { c.args } else { c.other }, 90) }
fn ctxbind(c: Ctx&, k: bool) {
    let r: i64[>..]& = if k { c.args } else { c.other };
    r.push(1);
}

// A function's own locals move out of its body's tail as out of a return
// (§4.1), branches and breaks included.
fn pick(c: bool) {
    var a: i64[>..] = [1];
    var b: i64[>..] = [2, 3];
    if c { a } else { b }
}
fn pick_typed(c: bool) -> i64[>..] {
    var a: i64[>..] = [4];
    var b: i64[>..] = [5, 6];
    if c { a } else { b }
}
fn local_tail() -> i64[>..] {
    var a: i64[>..] = [7, 8, 9];
    a
}
fn pick_break(c: bool) {
    var a: i64[>..] = [10];
    var b: i64[>..] = [20, 30];
    loop { if c { break a; } break b; }
}
fn pick_block(c: bool) -> i64[>..] {
    var a: i64[>..] = [40];
    var b: i64[>..] = [50, 60];
    block { if c { break a; } b }
}

fn main() {
    var a: i64[>..] = [1, 2, 3];
    var b: i64[>..] = [4, 5];
    let c = a.len > b.len;
    print(grow(if c { a } else { b }, 10), " ", grow(if !c { a } else { b }, 20), " ",
          a.len, " ", b.len);
    print(last(match a.len { 4 => b, _ => a }), " ",
          (block { if !c { break a; } b }).grow(30));
    print(grow(loop { break a; }, 40), " ", grow({ b }, 50), " ", a.len, " ", b.len);
    // A later break agrees with the first, and binds the same way.
    print(grow(block { if c { break a; } break b; }, 60), " ", a[a.len - 1]);
    let r .= first(if c { a } else { b });
    r = 99;
    let ra .= a;
    print(a[0], " ", grow(if c { ra } else { b }, 70), " ", a.len);
    let m: Msg.. = Msg.Text { s: "hello" };
    let n: Msg.. = Msg.Num { n: 7 };
    print(size(if c { m } else { n }), " ", size(if !c { m } else { n }), " ",
          sized(n, if c { a } else { b }));
    each(a, b, c) { v: i64[>..]& => v.push(80) };
    print(a[a.len - 1], " ", b.len);
    // Spelled out, the copy no longer sees the array it copied.
    let x = if c { copy(a) } else { copy(b) };
    a[0] = 1;
    print(x[0], " ", x.len, " ", pick(c).len, " ", pick_typed(!c)[1], " ", local_tail()[2]);
    // A bare block naming a variable-size array binds it the same way.
    var w: i64[] = [5, 6];
    bump({ w });
    ({ w }).bump();
    var ctx = Ctx { args: a, other: b };
    ctxbind(ctx, !c);
    print(w[0], " ", ctxgrow(ctx, c), " ", a.len, " ", b.len, " ", b[b.len - 1]);
    // A dispatched call binds such a construct at its other parameters too.
    stamp(m, if c { ra } else { b });
    print(a[a.len - 1], " ", a.len);
    print(pick_break(c)[0], " ", pick_break(!c)[1], " ", pick_block(c)[0], " ",
          pick_block(!c)[1]);
}
