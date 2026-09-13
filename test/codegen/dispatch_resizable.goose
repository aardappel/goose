// runtime-debug: dispatch uses the same resizable ABI as ordinary calls.
enum Choice { One, Two }
struct Bag { tag: i64, items: i64[>..] }
struct Nested { id: i64, bag: Bag }

fn build(c: Choice.One) -> i64[>..] { return [11, 12]; }
fn build(c: Choice.Two) -> i64[>..] { return [21, 22, 23]; }
fn bag(c: Choice.One) -> Nested {
    return Nested { id: 1, bag: Bag { tag: 10, items: [31, 32] } };
}
fn bag(c: Choice.Two) -> Nested {
    return Nested { id: 2, bag: Bag { tag: 20, items: [41, 42, 43] } };
}
fn pair(c: Choice.One) -> i64[>..], i64, Bag {
    return [51], 61, Bag { tag: 71, items: [81, 82] };
}
fn pair(c: Choice.Two) -> i64[>..], i64, Bag {
    return [52, 53], 62, Bag { tag: 72, items: [83] };
}
fn take(c: Choice.One, var xs: i64[>..], var b: Bag) -> i64 {
    xs.push(5);
    b.items.push(6);
    return xs.len + b.tag + b.items.len;
}
fn take(c: Choice.Two, var xs: i64[>..], var b: Bag) -> i64 {
    xs.push(7);
    b.items.push(8);
    return xs.len + b.tag + b.items.len;
}
var calls = 0;
fn input() -> i64[>..] { calls += 1; return [91, 92]; }
fn sum(xs: i64[:]) -> i64 { var n = 0; for x in xs { n += x; } return n; }

fn check(c: Choice) {
    var xs = build(c);
    xs.push(99);
    print(xs.len, xs[0], xs[xs.len - 1]);
    var appended: i64[>..] = [7];
    appended.append(build(c));
    appended.push(8);
    print(appended.len, sum(appended));
    var nested = bag(c);
    nested.bag.items.push(9);
    print(nested.id, nested.bag.tag, nested.bag.items.len, sum(nested.bag.items));
    let a, n, b = pair(c);
    print(a.len, a[0], n, b.tag, b.items.len);
    print(take(c, input(), Bag { tag: 100, items: [1] }), calls);
    print(sum(build(c)));
}
fn main() {
    check(Choice.One {});
    check(Choice.Two {});
}
