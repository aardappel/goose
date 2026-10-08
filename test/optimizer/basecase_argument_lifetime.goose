// runtime-debug: base-case inlining binds a self-call's arguments in front of
// the base case it inlines in the call's place. A slice argument views
// scratch owned outside the cycle (§7.8), filled just before the call and
// released after it: the view stays valid through the caller's statement, as
// the call would keep it -- while the recursing arm's callee allocates, and
// while the value the block yields still views it.

fn make(scratch: i64[>..<]&, k: i64) -> const i64[:] {
    let mark = scratch.len;
    scratch.append([k, k + 1, k + 2]);
    return scratch[mark..];
}

fn label(k: i64) -> const u8[:] {
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

recursive fn sum(s: const i64[:], n: i64, scratch: i64[>..<]&) -> i64 {
    if n == 0 { return 0; }
    let k = churn();
    let mark = scratch.len;
    let total = s[0] + k + sum(make(scratch, n * 10), n - 1, scratch);
    scratch.resize(mark);
    total
}

recursive fn sum_tail(s: const i64[:], n: i64, scratch: i64[>..<]&) -> i64 {
    if n == 0 { return 0; }
    let k = churn();
    let mark = scratch.len;
    let total = s[0] + k + sum_tail(make(scratch, n * 10)[1..], n - 1, scratch);
    scratch.resize(mark);
    total
}

// The labels are static text: a slice argument of a self-call needs no
// scratch at all.
recursive fn letters(s: const u8[:], n: i64) -> i64 {
    if n == 0 { return 0; }
    let k = churn();
    s.len * 10 + s[0] - 't' + k + letters(label(n), n - 1)
}

var seen: i64[>..] = [];

// The block's value is the argument itself in the base arm, and the callee's
// view of it in the other; either is read after churn() has allocated. The
// value views scratch, so no level releases its part: the caller does.
recursive fn keep(s: const i64[:], n: i64, scratch: i64[>..<]&) -> const i64[:] {
    if n == 0 { return s; }
    seen.push(keep(make(scratch, n * 100), n - 1, scratch)[churn()]);
    s
}

fn main() {
    var scratch: i64[>..<] = [];
    print(sum(make(scratch, 1), 3, scratch));
    scratch.clear();
    print(sum_tail(make(scratch, 1), 3, scratch));
    scratch.clear();
    print(letters(label(3), 2));
    print(keep(make(scratch, 7), 3, scratch)[1]);
    print(seen);
}
