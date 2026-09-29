// Code after a block or a loop runs in what holds at every exit of it that
// can be taken: the end of a block's body, a while's condition found false,
// a for's last iteration, and each break; where a bare guard skips the rest
// of a block, its condition is false. A loop exits at its breaks alone, so
// what all of them agree on holds after it, as a do-while's assignment does
// (§3.8, §4.4, §6.4).
var g = 5;
fn pick(i: i64) -> i64? { if i == 3 { return g; } return null; }

fn exits(c: i64) {
    var n: i64;
    block {
        if c == 0 { let a = 1; n = a; break; }
        guard c != 1 else { n = 2; break; }
        n = 3;
    }
    print(n);
}

fn guarded(c: bool) -> i64 {
    var x = 5;
    var y = 6;
    var o: i64? = null;
    if c { o .= x; }
    block {
        guard o == null;
        o .= y;
    }
    return o + 1;
}

fn valued(c: bool) -> i64 {
    var n: i64;
    let v = block {
        if c { break { n = 1; 4 }; }
        n = 2;
        5
    };
    return v + n;
}

fn looped() {
    var n: i64;
    var i = 0;
    loop {
        n = i * 3;
        if n > 10 { break; }
        i++;
    }
    print(n);
    var o: i64? = null;
    var j = 0;
    loop {
        o .= pick(j);
        if o { break; }
        j++;
    }
    print(o + 1);
}

fn loop_values(c: bool) -> i64 {
    var n: i64;
    let v = loop {
        if c { n = 3; break 1; }
        n = 5;
        break 2;
    };
    return v + n;
}

fn continued(c: bool) {
    var n: i64;
    var k = 0;
    loop {
        k++;
        block {
            if c && k < 3 { continue; }
            break;
        }
        n = k;
        break;
    }
    print(n);
}

fn narrowed_loops(var o: i64?) -> i64 {
    guard o else { return 0; }
    var i = 0;
    while i < 5 {
        i++;
        if i == 2 { break; }
    }
    for k in 3 {
        if k == i { break; }
    }
    return o + i;
}

fn main() {
    exits(0);
    exits(1);
    exits(2);
    print(guarded(false));
    print(guarded(true));
    print(valued(false));
    print(valued(true));
    looped();
    print(loop_values(false));
    print(loop_values(true));
    continued(false);
    continued(true);
    var z = 40;
    print(narrowed_loops(z));
}
