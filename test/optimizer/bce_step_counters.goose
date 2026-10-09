// Bounds checks on a counter that a counted loop steps by at most one per
// iteration (`lt += if less { 1 } else { 0 }`, `if keep { w += 1; }`): the
// counter cannot outrun the loop index it started at or below, so an index
// through it is in range wherever the loop index is. Every kept check here
// passes at runtime; bce_step_counters_abort.goose is one that fires.
import std;

// The branchless Lomuto partition of std's sort: every element is swapped
// with the boundary, which advances by the comparison's outcome.
fn partition_select(v: i64[:]) -> i64 {
    let p = v[0];
    var lt = 1;
    for i in 1..v.len {
        let x = v[i];
        v[i] = v[lt];                         // bce:elide
        v[lt] = x;                            // bce:elide
        lt += if x < p { 1 } else { 0 };
    }
    return lt - 1;
}

// The same partition stepping in a branch.
fn partition_branch(v: i64[:]) -> i64 {
    let p = v[0];
    var lt = 1;
    for i in 1..v.len {
        let x = v[i];
        v[i] = v[lt];                         // bce:elide
        v[lt] = x;                            // bce:elide
        if x < p { lt++; }
    }
    return lt - 1;
}

// Compaction over an array walk with an index: the write cursor trails it.
fn compact_odd(v: i64[:]) -> i64 {
    var w = 0;
    for x, i in v {
        if x % 2 == 1 {
            v[w] = x;                         // bce:elide
            w += 1;
        }
    }
    return w;
}

// A counter starting ahead of the index by a constant stays that far ahead
// at most, which `v.len - 1` as the loop's end makes room for.
fn ahead_by_one(v: i64[:]) -> i64 {
    var s = 0;
    var k = 1;
    for i in 0..v.len - 1 {
        s += v[k];                            // bce:elide
        k += if v[i] > 0 { 1 } else { 0 };
    }
    return s;
}

// The step may come after a continue: it runs at most once all the same.
fn step_after_continue(v: i64[:]) -> i64 {
    var w = 0;
    var s = 0;
    for i in v.len {
        s += v[w];                            // bce:elide
        if v[i] == 3 { continue; }
        w++;
    }
    return s;
}

// Two steps in one iteration can outrun the index.
fn two_steps(v: i64[:]) -> i64 {
    var w = 0;
    var s = 0;
    for i in v.len / 2 {
        s += v[w];                            // bce:keep
        w += 1;
        w += 1;
    }
    return s;
}

// A step inside a nested loop may run any number of times per iteration.
fn nested_step(v: i64[:]) -> i64 {
    var w = 0;
    var s = 0;
    for i in 2 {
        s += v[w];                            // bce:keep
        for j in 3 { w += 1; }
    }
    return s;
}

// A counter that is also set some other way is no step counter.
fn reset_counter(v: i64[:]) -> i64 {
    var w = 0;
    var s = 0;
    for i in v.len {
        s += v[w];                            // bce:keep
        w += 1;
        if i == 2 { w = 1; }
    }
    return s;
}

// Without a known start relation there is nothing to carry.
fn unknown_start(v: i64[:], w0: i64) -> i64 {
    var w = w0;
    var s = 0;
    for i in v.len {
        s += v[w];                            // bce:keep
        w += if i % 3 == 0 { 1 } else { 0 };
    }
    return s;
}

// Declared in the body, the counter starts over every iteration.
fn redeclared(v: i64[:]) -> i64 {
    var s = 0;
    for i in v.len {
        var w = v.len - 1 - i;
        s += v[w];                            // bce:keep
        w += 1;
    }
    return s;
}

// A step whose amount writes the counter itself is no unit step.
fn self_writing_step(v: i64[:]) -> i64 {
    var w = 0;
    var s = 0;
    for i in v.len {
        s += v[w];                            // bce:keep
        w += if { w = 0; i > 0 } { 1 } else { 0 };
    }
    return s;
}

// A counter a block writes may be stepped any number of times by its HOF.
fn captured_counter(v: i64[:]) -> i64 {
    var w = 0;
    var s = 0;
    for i in v.len {
        s += v[w];                            // bce:keep
        each_rev(v[..1]) { w += 1; };
    }
    return s;
}

// A while loop's counter moves as the body says, not one per iteration.
fn while_counter(v: i64[:]) -> i64 {
    var w = 0;
    var i = 0;
    var s = 0;
    while i < v.len {
        s += v[w];                            // bce:keep
        w += 1;
        i += 2;
        i -= 1;
    }
    return s;
}

fn main() {
    var a: i64[>..] = [5, 8, 1, 9, 3, 7, 2, 6, 4];
    let m = partition_select(a);
    print(m, " ", a);                                     // 4 [5, 1, 3, 2, 4, 8, 7, 9, 6]
    var b: i64[>..] = [5, 8, 1, 9, 3, 7, 2, 6, 4];
    let mb = partition_branch(b);
    print(mb, " ", b == a);                               // 4 true
    var c: i64[>..] = [1, 2, 3, 4, 5, 6, 7];
    print(compact_odd(c), " ", c[..4]);                   // 4 [1, 3, 5, 7]
    print(ahead_by_one([1, -1, 2, -2, 3]));               // 1
    print(step_after_continue([1, 3, 3, 4, 5]));          // 13
    print(two_steps([1, 2, 3, 4, 5, 6]));                 // 9
    print(nested_step([1, 2, 3, 4, 5]));                  // 5
    print(reset_counter([1, 2, 3, 4, 5]));                // 11
    print(unknown_start([1, 2, 3, 4, 5], 1));             // 15
    print(redeclared([1, 2, 3, 4]));                      // 10
    print(self_writing_step([1, 2, 3, 4]));               // 7
    print(captured_counter([1, 2, 3, 4]));                // 10
    print(while_counter([1, 2, 3, 4]));                   // 10
}
