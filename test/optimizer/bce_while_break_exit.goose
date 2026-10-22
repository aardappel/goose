// A while loop left by a break is left in the state the break was taken in.
// What the condition's evaluation stated holds there only as far as the body
// has not moved it since, and a break inside the condition leaves before the
// rest of the condition ran.
import std;

// The condition's completed check bounds i, which the body moves past it
// before it breaks.
fn find_zero(xs: i64[:]) -> i64 {
    var i = 0;
    while xs[i] != 0 {
        i += 1;
        if i == xs.len { break; }
    }
    return xs[i];                               // bce:keep (i == xs.len after the break)
}

// The condition's completed check bounds the length, which the body shrinks
// before it breaks.
fn trim(a: i64[>..<]&) -> i64 {
    while a[1] > 0 {
        a.pop();
        if a.len < 5 { break; }
    }
    return a[1];                                // bce:keep (a may be one long)
}

// A break in the condition leaves before the check after it ran.
fn guarded(xs: i64[:]) -> i64 {
    var i = 0;
    while { if i >= xs.len { break; } xs[i] != 0 } {  // bce:elide
        i += 1;
    }
    return xs[i];                               // bce:keep (i == xs.len after the break)
}

// Even of an index nothing in the loop moves.
fn first_check(xs: i64[:], k: i64, stop: bool) -> i64 {
    var n = 0;
    while { if stop { break; } xs[k] > n } {
        n += 1;
    }
    return xs[k];                               // bce:keep (the check of xs[k] never ran)
}

fn main() {
    var a: i64[>..] = [4, 0, 7];
    var b: i64[>..<] = [5, 5, 5, 5, 5, 5, 0];
    // Read from elements, so the call site states nothing about them.
    let k = a[0] - 2;
    let stop = a[1] > 0;
    print(find_zero(a[..]), " ", trim(b), " ", guarded(a[..]), " ", first_check(a[..], k, stop));
    print(find_zero([1, 2, 3]));
}
