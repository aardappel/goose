// runtime-define: GS_STACK_RESERVE=4194304
// An if ending its block whose else cannot complete normally, the shape
// `guard c else { ... }` parses to (§6.4), is emitted with the rest of the
// block after the else, not in a C block of its own. What the else, the rest
// and the condition take on data stacks still goes when each of them ends
// (§7.3): every loop below takes 80 KB an iteration, and a hundred of those
// kept would overflow a stack of the 4 MB reservation above.

fn big(k: i64) -> i64[>..] {
    var x: i64[>..] = [];
    x.resize(10000, k);
    x
}

// The else's storage goes at each of its exits, and the rest's at its end.
fn loops(n: i64) -> i64 {
    var sum = 0;
    for i in n {
        guard i % 4 != 0 else {
            let b = big(i);
            sum += b[9999];
            continue;
        }
        let r = big(1);
        sum += r[0];
    }
    var j = 0;
    while true {
        j++;
        guard j < n else {
            let b = big(2);
            sum += b[0];
            break;
        }
        let r = big(3);
        sum += r[5];
    }
    for i in n {
        sum += block {
            guard i % 3 != 0 else {
                let b = big(i);
                break b[1];
            }
            let r = big(i);
            r.len
        };
    }
    sum
}

// Returning from the else, and a run of guards each keeping what came
// before it.
fn early(i: i64) -> i64 {
    guard i % 5 != 0 else {
        let b = big(i);
        return b[2] + 1;
    }
    let r = big(i);
    r[3]
}
fn classify(i: i64) -> i64 {
    guard i % 2 == 0 else { return 1; }
    let a = big(i);
    guard i % 3 == 0 else { return 2 + a[0] - i; }
    let b = big(i);
    guard i % 5 == 0 else { return 3 + b[1] - i; }
    let c = big(i);
    a.len + b.len + c.len
}

// The condition's temporary lasts over the rest of the block, the statement
// the if is.
fn conds(n: i64) -> i64 {
    var sum = 0;
    for i in n {
        guard big(i)[7] % 2 == 0 else { continue; }
        let r = big(i);
        sum += r.len;
    }
    sum
}

// A resizable value built at the destination by the rest, the if being the
// function's tail.
fn built(i: i64) -> i64[>..] {
    guard i > 0 else { return []; }
    var r = big(i);
    r.push(i);
    r
}

// In a function value's body the else leaves the named function around it.
fn each<F>(n: i64) { for i in n { F(i); } }
fn stop_at(n: i64, at: i64) -> i64 {
    var seen = 0;
    each(n) {
        guard it < at else {
            let b = big(it);
            return b[0] * 1000 + seen;
        }
        let r = big(it);
        seen += r.len / 10000;
    }
    -1
}

fn main() {
    print(loops(200));
    var s = 0;
    for i in 200 { s += early(i) + classify(i); }
    print(s);
    print(conds(200));
    var total = 0;
    for i in 200 { total += built(i).len; }
    print(total);
    print(stop_at(200, 150), " ", stop_at(3, 5));
}
