// A bool function inlined into an `if` or `while` condition branches from
// where its value is decided (CodeGen::GenBranch). Its defers run on every
// one of those branches, the false ones included, as they do when its value
// is bound to a variable (§6.6, issue #431).

var calls = 0;

fn done(tag: string) {
    calls++;
    print("  done ", tag);
}

fn step(r: bool) -> bool { return r; }

fn plain(r: bool) -> bool {
    defer done("plain");
    return r;
}

fn and_chain(r: bool, x: bool) -> bool {
    defer done("and");
    return step(r) && x;
}

fn or_chain(r: bool) -> bool {
    defer done("or");
    return r || false;
}

fn bound(r: bool) -> bool {
    defer done("bound");
    let v = step(r);
    return v;
}

fn early(n: i64) -> bool {
    defer done("early");
    if n < 0 { return false; }
    return n > 2;
}

// Later defers run first.
fn two(r: bool) -> bool {
    defer done("first");
    defer done("second");
    return r;
}

// The defer next to a data-stack value: its restore and the deferred body
// both run on the way out.
fn with_text(r: bool) -> bool {
    var s: u8[>..] = "abc";
    defer done("text");
    s.push(100);
    return r && s.len == 4;
}

// An inlined call whose value is another inlined call: both defers run.
fn outer(r: bool) -> bool {
    defer done("outer");
    return plain(r);
}

fn show(name: string, b: bool) {
    print(name, if b { " -> then" } else { " -> else" });
}

fn main() {
    for r in [true, false] {
        print("r = ", r);
        if plain(r) { show("plain", true); } else { show("plain", false); }
        if !plain(r) { show("not plain", true); } else { show("not plain", false); }
        if and_chain(true, r) { show("and", true); } else { show("and", false); }
        if or_chain(r) { show("or", true); } else { show("or", false); }
        if bound(r) { show("bound", true); } else { show("bound", false); }
        if two(r) { show("two", true); } else { show("two", false); }
        if with_text(r) { show("text", true); } else { show("text", false); }
        if outer(r) { show("outer", true); } else { show("outer", false); }
        if plain(r) && plain(true) { show("plain && plain", true); }
        else { show("plain && plain", false); }
    }
    for n in [-1, 1, 3] {
        if early(n) { show("early", true); } else { show("early", false); }
    }
    var i = 0;
    while plain(i < 2) { i++; }
    print("i = ", i);
    var j = 0;
    while and_chain(true, j < 1) { j++; }
    print("j = ", j);
    print("calls = ", calls);
}
