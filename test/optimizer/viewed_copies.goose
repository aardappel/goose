// A call's by-value result, and a bare block's, if's or match's value, is a
// temporary copy wherever it is viewed rather than copied out (§9.2): sliced,
// indexed while the index runs, iterated, bound to a slice or reference
// parameter, a builtin's receiver, or a field or element of one viewed so. An
// inlined call reduced to the storage its body names, or a construct folded to
// its branch, must still be a copy there: each line reads the copy after that
// storage was written, or popped and refilled, and prints the same at every
// optimization level.
struct Pair { a: i64[3], n: i64 }

var g: i64[3] = [1, 2, 3];
var h: i64[3] = [4, 5, 6];
var gp = Pair { a: [1, 2, 3], n: 0 };
var grid: i64[3][2] = [[1, 2, 3], [4, 5, 6]];
var rows: i64[3][>..<] = [[1, 2, 3]];

fn get() -> i64[3] { g }
fn get_again() -> i64[3] { get() }
fn pair() -> Pair { gp }
fn cells() -> i64[3][2] { grid }
fn top() -> i64[3] { rows[0] }

// Overwrites everything the functions above return.
fn poke() -> i64 {
    g = [7, 8, 9];
    gp.a = [7, 8, 9];
    grid[0] = [7, 8, 9];
    0
}
fn poked() -> const i64[:] {
    poke();
    g[..]
}
fn reset() {
    g = [1, 2, 3];
    gp.a = [1, 2, 3];
    grid[0] = [1, 2, 3];
}

fn head(s: const i64[:], n: i64) -> i64 { s[0] * 10 + n }
fn head_ref(r: const i64[3]&, n: i64) -> i64 { r[0] * 10 + n }
fn head_copy(a: i64[3], n: i64) -> i64 { a[0] * 10 + n }
fn head_bytes(s: const u8[:], n: i64) -> i64 { s[0] as i64 * 10 + n }
fn head_refilled(s: const i64[:], n: i64[3]) -> i64 {
    rows.push([7, 7, 7]);
    s[0] * 100 + n[0]
}
// A view inside a body that is itself inlined, and folded again there.
fn head_inside() -> i64 { head(get()[..], poke()) }

fn main() {
    // top()[..] views a copy of the slot the pop frees and the push refills.
    print(head_refilled(top()[..], rows.pop()));
    print(head(get()[..], poke()));
    reset();
    print(head(get_again()[..], poke()));
    reset();
    print(head_inside());
    reset();
    print(get()[poke()]);
    reset();
    print(head(pair().a[..], poke()));
    reset();
    print(head(cells()[0][..], poke()));
    reset();
    print(head(pair().a, poke()));
    reset();
    print(head(cells()[0], poke()));
    reset();
    print(head_ref(pair().a, poke()));
    reset();
    print(head_ref(cells()[0], poke()));
    reset();
    // Copied out, not viewed: the argument is read before poke() runs.
    print(head_copy(pair().a, poke()));
    reset();
    print(head_copy(cells()[0], poke()));
    reset();
    var t = 0;
    for x in get() {
        t = t * 10 + x;
        poke();
    }
    reset();
    for &x in get() {
        t = t * 10 + x;
        poke();
    }
    reset();
    print(t);
    print(head_bytes(bytes_of(get()), poke()));
    reset();
    print(head_bytes(get().bytes_of(), poke()));
    reset();
    print(get()[..] == poked());
    reset();
    print(head((if true { g } else { h })[..], poke()));
    reset();
    print(head((match 1 { 1 => g, _ => h })[..], poke()));
    reset();
    print(head(({ g })[..], poke()));
    reset();
    t = 0;
    for x in ({ g }) {
        t = t * 10 + x;
        poke();
    }
    print(t);
}
