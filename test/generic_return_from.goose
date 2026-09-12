// runtime-debug: each specialization keeps its own long-distance return contract.
struct Box<T> { value: T }
fn echo<T>(x: T) -> T {
    fn leave() { return copy(x) from echo; }
    leave();
    return copy(x);
}
fn pair<T>(x: T) -> T, T {
    fn leave() { return x, x from pair; }
    leave();
    return x, x;
}
fn invoke<F>() { F(); }
fn via_block<T>(x: T) -> T {
    invoke() { return x; }
    return x;
}
fn recursive_echo<T>(x: T) -> T {
    recursive fn leave(n: i64) -> i64 {
        if n > 0 { return leave(n - 1); }
        return x from recursive_echo;
    }
    leave(2);
    return x;
}

// This helper has identical arguments at every call site. Its body still
// needs checking against each enclosing target's concrete return type.
fn finish() { return 7 from choose; }
fn relay() { finish(); }
fn choose<T>(x: T) -> T { relay(); return x; }

fn variable() -> i64[] { return [41, 42]; }
fn resizable() -> i64[>..] { return [51, 52, 53]; }
fn main() {
    var i: i64 = 11;
    var f: f64 = 2.5;
    var narrow: i16 = 12;
    print(echo(i));
    print(echo(f));
    print(echo(Box { value: i }).value);
    print(echo(Box { value: f }).value);
    let a, b = pair(i);
    print(a, b);
    let c, d = pair(f);
    print(c, d);
    print(via_block(i));
    print(via_block(f));
    print(recursive_echo(i));
    print(recursive_echo(f));
    print(choose(i), choose(narrow));
    var vs = echo(variable());
    var rs = echo(resizable());
    rs.push(54);
    print(vs.len, vs[1], rs.len, rs[3]);
}
