// References a struct holds beside its grow-shrink tail, or that lead to
// tables with grow-shrink storage of their own, are stored and kept used
// across a shrink where their types cannot lie in that storage's elements
// (§5.2): a view of a grow-only key buffer stored as a dictionary key, a
// value slice of static data kept while the dictionary's slots grow, a
// reference to a cell loaded out of a struct with a tail of references.
import std;
import dictionary;

let NAMES = ["John", "Jane"];

type Index = dictionary<const u8[:], i64>;
struct Keys { keys: u8[>..]&, index: Index&, r: u8[>..<] }

fn key(keys: u8[>..]&, i: i64) -> const u8[:] {
    let start = keys.len;
    format(keys, "k", i);
    return keys[start..];
}

fn fill(keys: u8[>..]&, index: Index&) { index.insert(key(keys, 2), 2); }
fn prep(b: Keys&) { fill(b.keys, b.index); }

type Vars = dictionary<u8[..20], const u8[:]>;
struct Render { vars: Vars&, r: u8[>..<] }

fn prepare(b: Render&) { b.vars.insert(str("k", 1), NAMES[1]); }

fn render(b: Render&) {
    var r: u8[>..] = [];
    let v = b.vars.get("k1");
    if v { r.append(v); }
    b.r = copy(r);
}

fn go(b: Render&) {
    prepare(b);
    for i in 3 { render(b); }
}

struct Cell { x: i32, y: i32 }
struct Maze { start: Cell& }
struct Walk { maze: Maze, steps: u32, path: (Cell&)[>..<] }

fn keep(c: Cell&) -> i64 {
    var stack: (Cell&)[>..<] = [];
    stack.push(c);
    return stack.len;
}

fn step(w: Walk&) { w.steps += keep(w.maze.start) as! u32; }

fn main() {
    var keys: u8[>..] = [];
    var index = Index {};
    var k = Keys { keys: keys, index: index, r: [] };
    prep(k);
    print(index.count, " ", keys);

    var vars = Vars {};
    var b = Render { vars: vars, r: [] };
    go(b);
    print(b.r);

    var c = Cell { x: 1, y: 2 };
    var w = Walk { maze: Maze { start: c }, steps: 0, path: [] };
    for i in 2 { step(w); }
    print(w.steps);
}
