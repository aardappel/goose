// A store into a field of what a reference variable points at lands in the
// storage that reference's exact root owns, so the facts about other
// storage stand, as after a store through the root variable itself. A
// parameter's root class, or a reference read out of a field on the way,
// leaves the storage unknown. The functions are `recursive` so that none
// is inlined: a call uses its callee's summary, whatever the level.
import std;

struct S { n: i64, arr: i64[>..] }
struct O { m: i64, s: S }
struct H { m: i64, r: S& }

var ga: i64[>..] = [1, 2, 3, 4];
var gs = S { n: 0, arr: [1, 2, 3] };

recursive fn through_local(k: i64) -> i64 {
    var s = S { n: k, arr: [5, 6] };
    let q: S& = s;
    var t = 0;
    if ga.len > 2 && q.arr.len > 1 {
        let p: S& = s;
        p.arr = [7];
        t = ga[2];                      // bce:elide (p's pointee is s, which ga is not)
        t += q.arr[0];                  // bce:keep (q.arr is s.arr, replaced through p)
    }
    return t;
}

recursive fn nested_fields(k: i64) -> i64 {
    var o = O { m: k, s: S { n: k, arr: [5, 6] } };
    let q: S& = o.s;
    var t = 0;
    if ga.len > 3 && o.s.arr.len > 1 {
        let p: O& = o;
        p.s.arr = [7];
        t = ga[2];                      // bce:elide (p's pointee is o)
        t += q.arr[0];                  // bce:keep (q.arr is o.s.arr)
        q.arr = [8, 9];
        t += ga[3];                     // bce:elide (q's pointee lies in o)
        t += o.s.arr[1];                // bce:keep (o.s.arr was replaced through q)
    }
    return t;
}

// What a callee writes through a field of its parameter's pointee is the
// argument's storage, which its summary names.
recursive fn set_arr(s: S&, v: i64) { s.arr = [v]; }

recursive fn summarized(k: i64) -> i64 {
    var s = S { n: k, arr: [5, 6] };
    var t = 0;
    if ga.len > 2 && s.arr.len > 1 {
        set_arr(s, k);
        t = ga[2];                      // bce:elide (set_arr writes s)
        t += s.arr[0];                  // bce:keep (set_arr replaced s.arr)
    }
    return t;
}

// A reference this body was handed names none of its own variables.
recursive fn handed(xs: i64[>..]&, k: i64) -> i64 {
    var s = S { n: k, arr: [5, 6] };
    var t = 0;
    if xs.len > 2 {
        let p: S& = s;
        p.arr = [7];
        t = xs[2];                      // bce:elide (xs was formed before s)
    }
    return t;
}

recursive fn stored_ref(k: i64) -> i64 {
    var h = H { m: k, r: gs };
    let p = &h;
    var t = 0;
    if gs.arr.len > 2 {
        p.r.arr = [k, k, k];
        t = gs.arr[2];                  // bce:keep (p.r is gs)
    }
    return t;
}

recursive fn through_param(p: S&, k: i64) -> i64 {
    var t = 0;
    if gs.arr.len > 2 {
        p.arr = [k];
        t = gs.arr[2];                  // bce:keep (p may be gs)
    }
    return t;
}

fn main() {
    var s = S { n: 1, arr: [1, 2, 3] };
    print(through_local(3), " ", nested_fields(3), " ", summarized(3), " ", handed(ga, 3), " ",
          stored_ref(4), " ", through_param(s, 5));
    print(through_param(gs, 9));
}
