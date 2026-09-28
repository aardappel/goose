// restore clears a grow-only array while execute, which calls it, holds a
// view named `c`, and restore's loop binder is named `c` too. Only execute's
// own code can name its `c`, and it is judged at each of execute's calls of
// restore -- the one in an if's condition too -- against what restore's
// summary says it shrinks (§5.1). Reduced from a game's command loop.
import std;

struct Blueprint { name: u8[..48], xs: i32[] }
var blueprints: Blueprint[>..] = [];
var cells: i32[>..] = [];

fn restore(n: i64) -> bool {
    for s in n {
        for c in 3 { cells.push(c as i32); }
        blueprints.clear();
    }
    return true;
}

fn execute(line: u8[:]) -> bool {
    let words = split(line, ' ');
    let c = words[0];
    if c == "load" { let r = restore(2); print(r); }
    if !restore(1) { return false; }
    return true;
}

fn main() {
    blueprints.push(Blueprint { name: "belt", xs: [1, 2] });
    var line: u8[>..] = "load x";
    let r = execute(line);
    print(r, " ", blueprints.len, " ", cells.len);
}
