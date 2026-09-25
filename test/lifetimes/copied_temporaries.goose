// The value of an if, match, block, loop or bare { } is a temporary copy of
// what the branch taken produced, even where that branch names a variable,
// and so are a function value's call and an array default<T>() (§9.2). A
// view of one may be passed down, and the storage it was copied from may
// shrink and be refilled while the view lasts. What one holds points where
// the value it copied pointed, so a slice read out of one can be kept
// wherever that value's could (§9.5).
struct Named { name: const u8[:] }
enum Item { Tagged { name: const u8[:] }, Blank }

var keep: const u8[:][>..] = [];
var names: Named[>..] = [];
var items: Item[>..] = [];
var gn = Named { name: "gn" };
var gi: Item = Item.Tagged { name: "gi" };
var words: const u8[:][2] = ["w1", "w2"];
var nums: i64[3] = [1, 2, 3];
var rows: i64[3][>..<] = [[1, 2, 3]];
var fills = 0;

fn total(xs: const i64[:]) -> i64 {
    var t = 0;
    for x in xs { t += x; }
    t
}
// Frees the slot of rows' only element and pushes a new one into it.
fn refill() -> i64 {
    rows.pop();
    fills += 1;
    rows.push([fills * 10, fills * 10 + 1, fills * 10 + 2]);
    0
}
fn head(xs: const i64[:], n: i64) -> i64 { xs[0] * 100 + n }
fn second(r: const i64&, n: i64) -> i64 { r * 100 + n }

// A global's contents, and a holder parameter's, which the caller may keep.
fn global_name(c: bool) -> const u8[:] { (if c { gn } else { gn }).name }
fn param_name(n: Named) -> const u8[:] { (match n.name.len { 0 => n, _ => n }).name }
fn ref_name(n: Named&) -> const u8[:] { (block { n }).name }
fn loop_name(n: Named&) -> const u8[:] { (loop { break n; }).name }
fn default_name() -> const u8[:] { default<Named[2]>()[1].name }
fn body_name<F>() -> const u8[:] { F().name }
fn body_head<F>() -> i64 { head(F()[..], refill()) }

fn main() {
    let c = keep.len == 0;
    for w in (if c { words } else { words }) { keep.push(w); }
    keep.push((block { words })[1]);
    keep.push((loop { break gn; }).name);
    keep.push(({ gn }).name);
    names.push(if c { gn } else { gn });
    match (if c { gi } else { gi }) {
        Tagged t => items.push(t),
        Blank => {}
    }
    print(global_name(c), " ", param_name(gn), " ", ref_name(gn), " ", loop_name(gn), " ",
          default_name().len);
    for w in keep { print(w); }
    print(names.len, " ", items.len);
    print(total((if c { nums } else { nums })[..]), " ", total((block { nums })[1..]), " ",
          total(default<i64[4]>()[..]));
    // Each line reads the copy after refill() freed and refilled the slot it
    // was copied from; the if and match on a constant are folded.
    print(head((if c { rows[0] } else { rows[0] })[..], refill()));
    print(head((if true { rows[0] } else { rows[0] })[..], refill()));
    print(head((match 0 { 0 => rows[0], _ => rows[0] })[..], refill()));
    print(head(({ rows[0] })[..], refill()));
    print(head((block { rows[0] })[..], refill()));
    print(head((loop { break rows[0]; })[1..], refill()));
    print(second((if true { rows[0] } else { rows[0] })[2], refill()));
    var t = 0;
    for x in ({ rows[0] }) {
        t = t * 100 + x;
        refill();
    }
    print(t);
    print(body_name() { gn });
    print(body_head() { rows[0] });
}
