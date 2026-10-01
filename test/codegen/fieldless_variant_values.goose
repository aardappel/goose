// A variant type with no fields is a value of its own (§3.4), though never
// a field or an element: a global, locals, parameters and results, branch
// values, references, case functions' parameters, and the ADT it adapts to
// in either mode. It takes no bytes behind its ADT's tag, so a Mode.. of
// one is the tag alone.
enum Mode { Off, Idle {}, Level { v: i64 } }

var last = Mode.Off {};

fn off() -> Mode.Off { Mode.Off {} }
fn same(o: Mode.Off) -> Mode.Off { o }
fn pick(c: bool) -> Mode.Idle { if c { Mode.Idle {} } else { default<Mode.Idle>() } }
fn seen(o: Mode.Off&) -> Mode.Off { o }

fn level(o: Mode.Off) -> i64 { 0 }
fn level(i: Mode.Idle) -> i64 { 1 }
fn level(l: Mode.Level) -> i64 { l.v }

fn main() {
    let o = off();
    var p = same(o);
    p = last;
    last = seen(p);
    let i = pick(true);
    let j = pick(false);
    print(o, " ", p, " ", last, " ", i, " ", o == p, " ", i == j, " ", same(o) == off());
    let m: Mode = o;
    let n: Mode = i;
    let vm: Mode.. = j;
    let modes: Mode[3] = [m, Mode.Level { v: 7 }, n];
    var vms: Mode..[] = [copy(vm), Mode.Off {}, Mode.Level { v: 9 }];
    for x in modes { print(x, " ", level(x)); }
    for x in vms {
        match x {
            Off => print("off"),
            Idle &d => print(d, " ", seen(o)),
            Level l => print(l.v),
        }
    }
    match n { Idle d => print(d == i), _ => print("?") }
    print(vms.len, " ", m == modes[0], " ", n == m);
}
