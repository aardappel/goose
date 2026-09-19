// runtime-debug: base-case inlining binds a self-call's arguments in front of
// the base case it inlines in the call's place. A slice argument that views a
// temporary keeps it alive through the caller's statement, as the call would
// have: while the recursing arm's callee allocates, and while the value the
// block yields still views it.

fn make(k: i64) -> i64[>..] {
    var r: i64[>..] = [k, k + 1, k + 2];
    return r;
}

fn label(k: i64) -> u8[] {
    if k == 1 { return "one"; }
    if k == 2 { return "two"; }
    "many"
}

// Allocates over whatever a released temporary left behind; returns 0.
fn churn() -> i64 {
    var fill: i64[>..] = [-1, -1, -1, -1, -1, -1, -1, -1];
    var text: u8[>..] = "????????????????????????????????";
    fill.len + text.len - 40
}

recursive fn sum(s: const i64[:], n: i64) -> i64 {
    if n == 0 { return 0; }
    let k = churn();
    s[0] + k + sum(make(n * 10), n - 1)
}

recursive fn sum_tail(s: const i64[:], n: i64) -> i64 {
    if n == 0 { return 0; }
    let k = churn();
    s[0] + k + sum_tail(make(n * 10)[1..], n - 1)
}

recursive fn letters(s: const u8[:], n: i64) -> i64 {
    if n == 0 { return 0; }
    let k = churn();
    s.len * 10 + s[0] as i64 - 't' as i64 + k + letters(label(n), n - 1)
}

var seen: i64[>..] = [];

// The block's value is the argument itself in the base arm, and the callee's
// view of it in the other; either is read after churn() has allocated.
recursive fn keep(s: const i64[:], n: i64) -> const i64[:] {
    if n == 0 { return s; }
    seen.push(keep(make(n * 100), n - 1)[churn()]);
    s
}

fn main() {
    print(sum(make(1), 3));
    print(sum_tail(make(1), 3));
    print(letters(label(3), 2));
    print(keep(make(7), 3)[1]);
    print(seen);
}
