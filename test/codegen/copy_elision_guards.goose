// A fixed-size value above CodeGen::FIXED_PTR_ABI_LIMIT goes to a call by
// pointer, and the code generator leaves out the copies the language makes
// (§7.2, §7.3) where it finds nothing can tell. Each case here prints
// something else if one of the conditions for that is left out: the comment
// at each condition names its case, here or in fixed_pointer_abi,
// last_use_moves, stable_later, lent_result_unfresh and movable_lent_param.
// `recursive` keeps a callee a call at -O2.
import std;

struct Big { a: i64 = 0, b: i64 = 0, c: i64 = 0, rest: i64[40] = [0; 40] }

recursive fn bump(var d: Big) -> Big { d.a += 1; d }
recursive fn ro_ref(d: Big, r: Big&) -> i64 { r.a = 50; d.a }
recursive fn ro_then<F>(d: Big) -> i64 { let k = F(1); d.a * 100 + k }

var g = Big { a: 1, .. };

// The block runs inside ro_then, where it writes the variable that is
// ro_then's argument: lent, the callee would see the write (StableArg's
// `captured`). Named once here, as the block's body belongs to ro_then's
// specialization, it would be handed over too (MovableArg's `captured`).
fn captured_lent() {
    var w = Big { a: 1, .. };
    print("captured_lent ", ro_then(w) { w.a = 77; it }, " ", w.a);
}
fn captured_moved() {
    var w = Big { a: 1, .. };
    print("captured_moved ", ro_then(w) { w.a = 77; it });
}

// The variable is bound to a writable reference the callee writes through
// (StableArg's `refwrite`).
fn ref_lent() {
    var r = Big { a: 1, .. };
    print("ref_lent ", ro_ref(r, r), " ", r.a);
}

// A global named once in a function is still read elsewhere: it is neither
// handed over to a `var` parameter (MovableArg's `isglobal` and `ownerspec`,
// either of which is enough) nor lent (StableArg's).
fn global_once() -> i64 { bump(g).a }

fn main() {
    captured_lent();
    captured_moved();
    ref_lent();
    print("global_once ", global_once(), " ", g.a);
}
