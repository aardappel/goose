// A `let` reference global, and a `let` or `const` slice global of a const
// type, points where its initializer said for good, so clearing a global
// array it does not view needs no care for it (§5.1): a save path, a
// constant, and views of another global's elements, beside an array of
// records whose bytes and integers any of them could otherwise view.
struct B { name: u8[..8], v: i64 }
let SAVE_PATH = "loot.save";
const EXT = ".bak";
var counts: i64[4] = [1, 2, 3, 4];
let FIRST: i64& = counts[0];
let SOME: const i64[:] = counts[1..3];
var blueprints: B[>..] = [];

fn reload() {
    blueprints.clear();
    blueprints.push(B { name: "sword", v: 3 });
}

fn main() {
    blueprints.push(B { name: "axe", v: 1 });
    blueprints.push(B { name: "bow", v: 2 });
    reload();
    blueprints.pop();
    print(SAVE_PATH, EXT, " ", blueprints.len, " ", FIRST, " ", SOME[1]);
}
