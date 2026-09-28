// error: cannot call restore, which shrinks blueprints while c is still used
// restore's body is checked inside execute's check, at its first call, and
// execute's variables are still judged at that call, from restore's summary:
// execute's `c` views an element of blueprints and is used after the call
// (§5.1). restore's own scan sees only its own variables, a loop binder also
// named `c` among them.
struct Blueprint { name: u8[..48], count: i32 }
var blueprints: Blueprint[>..] = [];
var cells: i32[>..] = [];

fn restore(n: i64) -> bool {
    for s in n {
        for c in 3 { cells.push(c as i32); }
        blueprints.clear();
    }
    return true;
}

fn execute() -> bool {
    let c = blueprints[0].name[..];
    let r = restore(1);
    print(r, c);
    return true;
}

fn main() {
    blueprints.push(Blueprint { name: "belt", count: 2 });
    print(execute());
}
