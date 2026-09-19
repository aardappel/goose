// The value of an if, match, block or loop may view what outlives the
// construct, wherever the value goes: storage of an enclosing scope, the
// caller's storage behind a parameter, or a temporary of the construct's own
// statement, as a match's scrutinee is (§9.2).
struct View { s: i64[:] }
enum Slot { Full { vals: i64[3] }, Empty }

fn mk_slot(k: i64) -> Slot.. {
    if k > 0 { Slot.Full { [k * 10, k * 20, k * 30] } } else { Slot.Empty }
}
// Grows a data stack over where a released argument would have been.
fn second(s: i64[:]) -> i64 {
    var grow: i64[>..] = [100, 200, 300];
    grow.push(400);
    s[1] + grow[0] * 0
}
fn held(v: View) -> i64 { second(v.s) }
fn pick(a: i64[:], b: i64[:], first: bool) -> i64[:] {
    if first { let k = 0; a[k..] } else { block { if b.len > 0 { break b; } a } }
}

fn main() {
    var xs: i64[>..] = [1, 2, 3];
    var ys: i64[>..] = [4, 5, 6];
    var zs: i64[>..] = [7, 8, 9];
    let c = xs[0] > 0;
    print(second(if c { let k = 1; ys[k - 1..] } else { xs[..] }));
    print(second({ var n = 0; n += 1; zs[n - 1..] }));
    print(second(block { if !c { break ys[..]; } xs[..] }));
    print(second(loop { var n = 1; if n > 0 { break ys[n..]; } }));
    print(second(match xs[0] { 1 => zs[1..], _ => xs[..] }));
    print(held(if c { var k = 2; View { s: xs[k - 2..] } } else { View { s: ys[..] } }));
    print(second(pick(xs[..], zs[..], !c)));
    // A local of an outer branch outlives a construct inside that branch.
    print(if c { var t: i64[>..] = [10, 11, 12]; second(if c { t[..] } else { xs[..] }) } else { 0 });
    // The scrutinee lasts until the end of the statement, past the call its
    // view is passed to.
    print(second(match mk_slot(xs[0]) { Full &f => f.vals[..], Empty => xs[..] }));
}
