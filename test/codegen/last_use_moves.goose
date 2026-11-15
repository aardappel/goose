// A by-value argument above CodeGen::FIXED_PTR_ABI_LIMIT is passed as a
// pointer to the callee's copy. Where the argument is a variable of the
// caller's named nowhere else (CodeGen::SingleUses), the callee is given
// the variable's own storage instead (CodeGen::MovableArg). The callee's
// argument is still its own value (§7.2): each case here goes wrong if the
// variable is read, or reachable, after the callee has changed it. Last uses
// after other uses, on a branch or in a loop are copied too, for now.
import std;

struct Big { a: i64 = 0, b: i64 = 0, rest: i64[520] = [0; 520] }

fn mk(k: i64) -> Big { Big { a: k, .. } }
fn bump(var d: Big) -> Big { d.a += 100; d.b += 1; d }
fn spoil(var d: Big) -> i64 { d.a = -1; d.b }
fn both(var d: Big, e: Big) -> i64 { d.a = -1; e.a }
fn through(r: Big&, var d: Big) -> i64 { d.a = -1; r.a }
// A parameter at its only use is handed on: the caller's copy, not the
// caller's variable.
fn pass_on(var d: Big) -> i64 { spoil(d) }
fn read_after(var d: Big) -> i64 { let k = spoil(d); d.a * 10 + k }

// Read through an address taken before the call that hands it over, and
// used after it: an element, a slice, a read-only reference argument.
fn peekc(r: const Big&, k: i64) -> i64 { r.a + k }
fn sum0(s: const i64[:], k: i64) -> i64 { s[0] + k }
fn wipe(var d: Big) -> i64 { d.a = -1; d.rest[0] = 99; 0 }
fn index_after() { let ix = mk(1); print("index_after ", ix.rest[wipe(ix)]); }
fn ref_arg() { let px = mk(1); print("ref_arg ", peekc(px, wipe(px))); }
fn slice_arg() { let sx = mk(1); print("slice_arg ", sum0(sx.rest, wipe(sx))); }
// Read through a reference by a deferred body, and by a nested function.
fn ref_deferred() {
    let dx = mk(1);
    let r: const Big& = dx;
    defer { print("ref_deferred ", r.a); }
    print("ref_deferred_wipe ", wipe(dx));
}
fn ref_captured() {
    let cx = mk(1);
    let r: const Big& = cx;
    fn look() -> i64 { r.a }
    fn spoil2(var d: Big) -> i64 { d.a = -1; look() }
    print("ref_captured ", spoil2(cx));
}

// A read-only parameter its callers may lend a variable of their own for,
// as it is read twice here and never written: its last use is the caller's
// variable, not this function's to give.
fn hand_on(d: Big) -> i64 { spoil(d) }
fn lent(d: Big) -> i64 { let k = d.a; hand_on(d) + k }

var gb = mk(50);

fn main() {
    // Handed over at its only use.
    let a = mk(1);
    print("last ", bump(a).a);
    // Read again after.
    let b = mk(2);
    let b2 = bump(b);
    print("after ", b.a, " ", b2.a);
    // Declared outside a loop, used in it: each time round reads it.
    let c = mk(3);
    for i in 3 { print("loop ", spoil(c), " ", c.a); }
    var s = 0;
    for i in 3 { s += bump(c).a; }
    print("loop_sum ", s, " ", c.a);
    // Declared in the loop: a new value each time round.
    for i in 2 { let dl = mk(10 + i); print("in_loop ", spoil(dl)); }
    // Read on one branch after.
    let e = mk(4);
    let k = spoil(e);
    if k == 0 { print("branch ", e.a); }
    // Last on one branch, read on the other.
    let f = mk(5);
    if k > 0 { print("never ", spoil(f)); } else { print("other ", f.a); }
    let f2 = mk(6);
    if k == 0 { print("then ", spoil(f2)); } else { print("else ", f2.a); }
    // Reassigned from its own call, and replaced after its last use.
    var g = mk(7);
    g = bump(g);
    print("reassigned ", g.a);
    let k2 = spoil(g);
    g = mk(8);
    print("replaced ", k2, " ", g.a);
    // Twice in one call, and through a reference in the same call.
    let h = mk(9);
    print("twice ", both(h, h));
    let hh = mk(11);
    let r: Big& = hh;
    print("through ", through(r, hh));
    // A reference into it read after.
    let j = mk(12);
    let rj: Big& = j;
    let kj = spoil(j);
    print("ref_after ", kj, " ", rj.a);
    // Parameters.
    let m = mk(13);
    print("pass_on ", pass_on(m), " ", m.a);
    print("read_after ", read_after(m), " ", m.a);
    let z = mk(30);
    print("lent ", lent(z), " ", z.a);
    // A global is never the caller's to give.
    print("global ", spoil(gb), " ", gb.a);
    // Short-circuit: the right side may not run, and the variable is read
    // after it.
    let n = mk(14);
    if k == 0 && spoil(n) == 0 { print("and ", n.a); }
    // A deferred body reads it at the block's end.
    {
        let q = mk(15);
        defer { print("deferred ", q.a); }
        print("before_defer ", spoil(q));
    }
    // Broken out of, and continued past.
    let w = mk(16);
    var t = 0;
    while true {
        t += 1;
        if t == 2 { continue; }
        if t > 3 { print("break ", spoil(w)); break; }
    }
    print("after_break ", w.a);
    // In a match arm, and a block's value.
    let ms = mk(18);
    let mv = match k { 0 => spoil(ms), _ => ms.a };
    print("match ", mv);
    let bl = mk(19);
    let bv = block { if k == 0 { break spoil(bl); } bl.a };
    print("block ", bv);
    index_after();
    ref_arg();
    slice_arg();
    ref_deferred();
    ref_captured();
    let w2 = mk(17);
    var u = 0;
    loop {
        u += 1;
        if u < 3 { continue; }
        print("loop_last ", spoil(w2));
        break;
    }
}
