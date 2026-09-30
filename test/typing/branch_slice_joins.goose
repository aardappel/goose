// Branch values that are arrays and slices of one element type join as a
// slice (§6.4): where another branch is a slice, or the construct's
// destination type is one, an array branch is a whole-array slice of the
// array itself rather than a copy, read-only where any branch is.
struct Item { name: u8[..48], count: i64 }

const table: u8[3] = [120, 121, 122];
const counts: i64[3] = [1, 2, 3];
var gnames: u8[..8][2] = ["ann", "bob"];
var glabel = if gnames[0].len > 2 { gnames[0] } else { "short" };

fn label(item: Item&) -> const u8[:] {
    if item.count == 0 { "none" } else { item.name }
}
fn label_inferred(item: Item&) {
    match item.count { 0 => "none", _ => item.name }
}
fn via_ref(r: u8[..8]&, c: bool) -> const u8[:] { if c { r } else { "nothing" } }

fn total(xs: i64[:]) -> i64 {
    var t = 0;
    for x in xs { t += x; }
    t
}
fn bump(xs: i64[:]) { xs[0] += 1; }
fn zero_first(r: i64[3]&, rest: i64[>..]&, c: bool) {
    let s = if c { r } else { rest[..] };
    s[0] = 0;
}
fn first<T>(xs: T[:]) -> T { xs[0] }
fn with_pick<F>(c: bool, a: i64[2]&, b: i64[>..]&) { F(if c { a } else { b }) }
fn with_same<F>(c: bool, a: i64[2]&, b: i64[2]&) { F(if c { a } else { b }) }
// Given a literal, xs is read-only in this instantiation: its join with a
// writable array is a u8[:] that no slot holds.
fn first_of(xs: u8[:], c: bool) -> u8 {
    var arr: u8[2] = [104, 105];
    let s: const u8[:] = (if c { xs } else { arr })[..];
    s[0]
}

fn main() {
    var sword = Item { name: "sword", count: 1 };
    var shield = Item { name: "shield", count: 0 };
    print(label(sword), " ", label(shield), " ", label_inferred(sword), " ",
          label_inferred(shield), " ", glabel);                  // sword none sword none ann
    let c = sword.count > 0;

    // No destination type, the slice in either branch; an annotated one.
    let a = if c { sword.name } else { "none" };
    let b = if !c { sword.name } else { "none" };
    let d: const u8[:] = if c { sword.name } else { "none" };
    var name: u8[..8] = "zed";
    print(a, " ", b, " ", d, " ", via_ref(name, true), " ", via_ref(name, false));
                                                                 // sword none sword zed nothing

    // An else-if chain and match arms, over arrays of two capacities.
    var short: u8[..8] = "dagger";
    for k in 3 {
        let s = if k == 0 { short } else if k == 1 { sword.name } else { "fists" };
        let m = match k { 0 => short, 1 => sword.name, _ => "fists" };
        print(s, " ", m, " ", s.len);                            // dagger dagger 6, ...
    }

    // A view, not a copy: a later write to the array shows through it, and
    // a non-fixed array needs no copy().
    var nm: u8[..16] = "anna";
    let view = if c { nm } else { "nobody" };
    nm[0] = 'A';
    var buf: u8[>..] = [];
    format(buf, "gold ", 12);
    let g = if c { buf } else { "broke" };
    print(view, " ", g);                                         // Anna gold 12

    // Writable where every branch is, nested arrays of one type included.
    var nums: i64[3] = [1, 2, 3];
    var more: i64[>..] = [10, 20];
    var w: i64[:] = if c { nums } else { more };
    w[0] = 100;
    var cs: u8[4] = [65, 66, 67, 68];
    var cs1: u8[3] = [97, 98, 99];
    var cs2: u8[3] = [100, 101, 102];
    for k in 3 {
        let s = if k == 0 { cs[..] } else if k == 1 { cs1 } else { cs2 };
        s[0] = 88;
    }
    print(nums[0], " ", cs, " ", cs1, " ", cs2);                 // 100 XBCD Xbc Xef
    // An array behind a writable reference, where the reference points.
    var ra: i64[3] = [1, 2, 3];
    var rb: i64[>..] = [4, 5];
    zero_first(ra, rb, true);
    zero_first(ra, rb, false);
    print(ra, " ", rb);                                          // [0, 2, 3] [0, 5]

    // Read-only where a branch is.
    const t1 = if c { table } else { "abc" };
    var t2 = if !c { table } else { "abc" };
    t2 = "again";
    print(t1, " ", t2);                                          // xyz again

    // A slice parameter is the destination: arrays of any type there, a
    // generic one's and a function value's included, and a const array's
    // inside a typed slot.
    bump(if c { nums } else { more });
    let sum: i64 = total(counts) + total(if c { counts } else { more });
    print(nums[0], " ", total(if c { nums } else { more }), " ",
          first(if c { more } else { nums }), " ", sum);         // 101 106 10 12
    var n2: i64[2] = [1, 2];
    var n3: i64[2] = [7, 8];
    with_pick(true, n2, more) { xs: i64[:] => xs[0] = 50; };
    with_same(false, n2, n3) { xs: i64[:] => xs[1] = 80; };
    bump(if !c { n2 } else { n3 });
    print(n2[0], " ", n3[1], " ", n3[0]);                        // 50 80 8

    // As a builtin's argument, an operand, a for's sequence; arrays of
    // strings.
    print(if c { sword.name } else { "none" });                  // sword
    var hits = 0;
    if (if c { sword.name } else { "x" }) == "sword" { hits++; }
    for ch in if c { short } else { "abc" } { if ch == 'g' { hits++; } }
    let words: u8[][] = ["one", "two"];
    var extra: u8[][>..] = [];
    extra.push("three");
    for word in if c { words } else { extra[..] } { hits += word.len; }
    hits += (if c { sword.name } else { "x" }).bytes_of().len;
    print(hits);                                                 // 14

    // A block's tail against its breaks, a first break's slice, and views of
    // a grow-shrink array bound to variables.
    let e = block {
        if !c { break "early"; }
        sword.name
    };
    let f = loop {
        if !c { break "never"; }
        break sword.name;
    };
    var gs: u8[>..<] = [];
    gs.push(71);
    let h = block {
        if !c { break "none"; }
        break gs;
    };
    let h2 = if c { gs } else { "none" };
    print(e, " ", f, " ", h, " ", h2);                           // sword sword G G

    // A join inside another join's branch, and one of a read-only
    // instantiation's slice.
    let deep = if c { let inner = if !c { "x" } else { cs1 }; inner } else { cs2 };
    print(deep, " ", first_of("q", true), " ", first_of("q", false));   // Xbc 113 104
}
