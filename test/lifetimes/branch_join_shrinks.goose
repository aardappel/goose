// Shrinks inside branches joined as a slice (§6.4), and after them in the
// same statement, where nothing held points into the array shrunk (§5.1,
// §5.2): another array popped while an element is built with a joined tag,
// a grow-shrink pop in a joined branch, and one after a joined view of
// another array.
struct R { tag: const u8[:], data: u8[] }
var rows: R[>..] = [];
var nums: i64[>..] = [];
var gs: u8[>..<] = [];
var gname: u8[..8] = "nm";
fn show(s: const u8[:], last: u8) { print(s, " ", last); }
fn main() {
    rows.push(R { tag: "a", data: "x" });
    nums.push(5);
    nums.push(6);
    var name: u8[..8] = "local";
    let c = rows.len > 0;
    rows.push(R { tag: if c { nums.pop(); gname } else { "none" }, data: "y" });
    gs.push(71);
    gs.push(72);
    let s = if c { gs.pop(); name } else { "none" };
    show(if c { name } else { "x" }, gs.pop());
    print(s, " ", rows.len, " ", nums.len, " ", gs.len);
    for r in rows { print(r.tag, " ", r.data); }
}
