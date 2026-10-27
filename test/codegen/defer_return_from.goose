// Defers and long-distance returns (§6.6, §7.9). A `return ... from`
// passing through a frame runs that frame's defers after the values are in
// the target's channels; the target runs its own at the catch, after the
// value is at its destination; the returning function runs its own after
// building the values. A defer body may allocate and call freely meanwhile,
// so long as it uses no long-distance return machinery itself.
import std;
import exception;

fn inner(n: i64) -> i64 {
    var scratch: u8[>..] = str("inner ", n);
    defer print("inner done: ", scratch);
    if n < 0 { return -n from outer; }
    return n;
}

fn mid(n: i64) -> i64 {
    var buf: i64[>..] = [n, n + 1];
    defer {
        // Allocation and calls on the propagation path.
        var t: u8[>..] = str("mid done: ", buf.len, " ", buf[1]);
        t.push(33);
        print(t);
    }
    let r = inner(n);
    buf.push(r);
    return buf.len;
}

fn outer(n: i64) -> i64 {
    defer print("outer done");
    let r = mid(n);
    return r * 100;
}

// A nonfixed value in flight sits on the target's destination stack; the
// frames between build and release their own storage around it.
recursive fn deep_text(n: i64) -> u8[>..] {
    var local: u8[..8] = "local";
    defer print("deep_text done: ", local);
    if n == 0 { return str("from depth ", n) from top_text; }
    local.push(43);
    return deep_text(n - 1);
}

fn mid_text(n: i64) -> u8[>..] {
    var filler: u8[>..] = [0; 100];
    defer print("mid_text done: ", filler.len);
    var r = deep_text(n);
    r.push(46);
    return r;
}

fn top_text(n: i64) -> u8[>..] {
    defer print("top_text done");
    var text = mid_text(n);
    text.append(" (normal)");
    return text;
}

// A long-distance return landing over a named result: the target's defer
// runs after the caught value is at the destination.
fn landing(n: i64) -> u8[>..] {
    var result: u8[>..] = "partial";
    defer print("landing done");
    if n > 0 { result.append(str(" ", n)); }
    deep_landing(n);
    return result;
}

fn deep_landing(n: i64) {
    defer print("deep_landing done");
    if n > 5 { return "replaced" from landing; }
}

// Several frames with defers between a throw and its handler, through the
// exception module.
fn step3(n: i64) -> i64 {
    defer print("step3 done ", n);
    if n > 2 { exception::throw(str("too big: ", n)); }
    return n;
}

fn step2(n: i64) -> i64 {
    var items: i64[>..] = [];
    defer print("step2 done ", items.len);
    items.push(step3(n));
    items.push(step3(n + 1));
    return items.len;
}

fn step1(n: i64) -> i64 {
    defer print("step1 done");
    return step2(n) + step2(n + 1);
}

// The library's try with a finally, a defer ahead of the try: the cleanup
// runs after a normal completion, after a caught throw, and after a
// long-distance return that leaves through the call.
fn may_throw(n: i64) {
    if n == 1 { exception::throw("inner error"); }
    print("ran ", n);
}

fn finally_demo(n: i64) -> i64 {
    fn body() {
        if n == 2 { return 7 from finally_demo; }
        may_throw(n);
    }
    fn catch_text(e: u8[]&) { print("caught text: ", e); }
    fn finally() { print("finally ", n); }
    exception::try<u8[]>(body, catch_text, finally);
    return 0;
}

fn main() {
    print(outer(3));
    print(outer(-4));
    print(top_text(2));
    print(top_text(0));
    print(landing(1));
    print(landing(9));
    fn run1() { print(step1(1)); }
    fn run2() { print(step1(2)); }
    exception::try<u8[]>(run1) { e => print("caught: ", e); };
    exception::try<u8[]>(run2) { e => print("caught: ", e); };
    for n in 3 { print("finally_demo ", n, " -> ", finally_demo(n)); }
}
