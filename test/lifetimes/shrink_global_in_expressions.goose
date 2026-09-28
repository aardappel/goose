// Grow-only shrinks of a global array inside larger expressions -- an
// argument, an if's condition, an assert, both sides of && and an
// initializer's operand -- beside a let save path and a global holding
// views of another global array only: judged against the other globals as
// statements are (§5.1).
struct B { name: u8[..8], v: i64 }
let SAVE_PATH = "loot.save";
var bs: B[>..] = [];
var others: B[>..] = [];
var CS: const u8[:][>..] = [];
fn later() { CS.push(others[0].name[..]); }
fn reload() -> bool {
    bs.clear();
    bs.push(B { name: "sword", v: 3 });
    true
}
fn drop_last() { print("dropped ", bs.pop().name, " of ", bs.len + 1); }
fn main() {
    bs.push(B { name: "axe", v: 1 });
    bs.push(B { name: "bow", v: 2 });
    others.push(B { name: "other", v: 9 });
    later();
    drop_last();
    if !reload() { print("failed"); }
    assert(reload());
    let both = reload() && bs.len == 1;
    bs.push(B { name: "cap", v: 4 });
    let n = bs.pop().v * 10 + 1;
    print(SAVE_PATH, " ", both, " ", n, " ", bs.len, " ", CS[0]);
}
