// A tour of the core language in one linear program: values and their
// widths, the array family, slices, structs, control flow as expressions,
// functions with several results, optionals and references. The other
// samples assume what this one shows.
//
// Build and run (from this directory):
//   goose -o tour.c 01_tour.goose && cl tour.c && tour

// A function taking a slice accepts every array kind and any sub-range of
// one, without a copy: the slice is a reference to the elements plus a count.
fn total(xs: i64[:]) -> i64 {
    var t = 0;
    for x in xs { t += x; }
    return t;
}

// Several results are a calling convention, not a tuple type. By custom a
// trailing bool (or error code) says whether the rest is meaningful.
fn divide(a: i64, b: i64) -> i64, bool {
    if b == 0 { return 0, false; }
    return a / b, true;
}

// "Maybe an element": an optional is a nullable reference into the input.
fn first_at_least(xs: i64[:], min: i64) -> i64? {
    for &x in xs { if x >= min { return x; } }
    return null;
}

// Untyped parameters make a function generic; each call instantiates it for
// the argument types it gets.
fn larger(a, b) { if a > b { a } else { b } }

// A struct is packed data in declaration order. Fields may carry defaults,
// which a literal can then leave out.
struct Point { x: f64, y: f64 }
struct Player { name: u8[..16], pos: Point, hp: i32 = 100, alive: bool = true }

fn main() {
    // --- scalars -----------------------------------------------------------
    // `let` binds a constant, `var` a variable. An integer literal adapts to
    // the type it meets; on its own it is an i64, the default for counts and
    // indices. The sized types are for data.
    let answer = 42;
    var count: i32 = 0;
    let byte: u8 = 255;
    let ratio = 0.75;
    let single: f32 = 1.5;
    let yes = 3 > 2;
    count += 1;
    count++;
    print("answer=", answer, " count=", count, " byte=", byte, " ratio=", ratio,
          " single=", single, " yes=", yes);

    // Arithmetic runs at the operands' own width: u8 + u8 is an 8-bit add.
    // A narrower type widens into a wider one implicitly; nothing narrows
    // without a cast.
    let wrapped: u8 = byte + 1;          // unsigned math wraps by definition
    let widened = byte + answer;         // the u8 widens into the i64
    let narrowed = answer as u8;         // range-checked in debug, truncates in release
    print(wrapped, " ", widened, " ", narrowed);
    // `%` is Euclidean, so the result is never negative: `x % n` is always
    // a valid index into an array of length n, whatever the sign of x.
    print(-7 % 3, " ", 7 % -3);

    // --- the array family --------------------------------------------------
    // Every array is inline, packed element data; the kinds differ in how
    // the size behaves. A fixed array has a constant size and no metadata.
    var fixed: i32[4] = [1, 2, 3, 4];
    fixed[0] = 10;
    let zeros: f64[8] = [0.0; 8];        // fill form
    // The grow-only array is the workhorse: a push is a pointer bump on the
    // data stack, nothing is ever moved or reallocated, and references into
    // it stay valid as it grows -- the promise a vector<T> cannot make, and
    // the one the data-structure samples build on.
    var grow: i64[>..] = [];
    for i in 5 { grow.push(i * i); }
    // A grow-shrink array also pops: the natural stack and queue.
    var stack: i64[>..<] = [];
    stack.push(1); stack.push(2); stack.push(3);
    let top = stack.pop();
    // A limited array has a fixed capacity, so it lives inline anywhere a
    // fixed value can: in a struct, as an array element, in a local.
    var small: u8[..8] = [];
    small.push('h'); small.push('i');
    // A variable array is sized at construction and frozen afterwards.
    let frozen: i64[] = [7, 8, 9];
    print(fixed, " ", zeros.len, " ", grow, " ", stack, " ", top, " ",
          small, " ", small.len, "/", small.cap, " ", frozen);

    // A string is a u8 array: no string type, no allocation. A literal is
    // static data, a builder is a growable u8 array, and `format`, `str`
    // and `print` render anything as text into one of those.
    var s: u8[>..] = [];
    s.append("hello");
    s.push(' ');
    format(s, "x", 1, " ", 2.5);
    print(s, " (", s.len, " bytes)");

    // --- slices ------------------------------------------------------------
    // `a[x..y]` is a view of elements x up to y; `^k` counts from the end.
    // A slice cannot dangle: the checker ties it to the array that owns the
    // elements, and that array cannot shrink under it.
    let middle = grow[1..4];
    let last_two = grow[^2..];
    print(middle, " ", last_two, " ", total(middle), " ", total(grow), " ", total(fixed_sum(fixed)));

    // --- structs -----------------------------------------------------------
    // A Player is one flat value: the name's bytes live inside it, not behind
    // a pointer, so an array of players is one contiguous block.
    var p = Player { name: "gus", pos: Point { 1.0, 2.0 } };
    p.hp -= 30;
    p.pos = p.pos + Point { 0.5, 0.5 };  // arithmetic is elementwise on same-typed structs
    print(p);
    print(p.name, " at ", p.pos.x, ",", p.pos.y, " hp ", p.hp);

    // --- control flow as expressions ---------------------------------------
    // `if` and `match` are expressions: no ternary, no temporary assigned in
    // each branch.
    let kind = if p.hp > 50 { "healthy" } else { "hurt" };
    let level = match p.hp { 0 => "dead", 1..50 => "low", _ => "fine" };
    print(kind, " ", level);
    // `guard` states what the code after it relies on; its else must leave.
    // `block` gives `break` a target, so a chain of checks can bail out with
    // a value without a loop.
    let verdict = block {
        guard p.alive else { break "gone"; }
        guard p.hp > 0 else { break "dead"; }
        "standing"
    };
    print(verdict);

    // --- loops -------------------------------------------------------------
    var acc = 0;
    for i in 3 { acc += i; }             // 0, 1, 2
    for i in 10..13 { acc += i; }        // 10, 11, 12
    for x in fixed { acc += x; }         // copies of the elements
    for &x in fixed { x *= 2; }          // references: the mutation form
    for x, i in frozen { acc += x * i; } // with the index
    var n = 0;
    while n < 3 { n++; }
    loop { n--; if n == 0 { break; } }
    print(acc, " ", fixed, " ", n);

    // --- several results, optionals, references ----------------------------
    // Two results come back together, as part of the calling convention:
    // no tuple type, nothing packed and unpacked.
    let q, ok = divide(7, 2);
    let q2, ok2 = divide(1, 0);
    print(q, " ", ok, " ", q2, " ", ok2);
    // An optional must be narrowed before use: inside the `if` it is a plain
    // reference to the element it found.
    let big = first_at_least(grow, 5);
    if big { print("first >= 5 is ", big); } else { print("none"); }
    let none = first_at_least(grow, 100);
    print(none == null);
    // A reference reads and writes as its target; `.=` binds it (where `=`
    // would copy the element) and retargets it, and `.==` asks whether two
    // references name the same target.
    var r .= grow[0];
    r = 100;
    r .= grow[1];
    r += 1;
    print(grow, " ", grow.index_of(r), " ", r .== grow[1]);
    print(larger(3, 9), " ", larger(2.5, 1.5));
}

// A fixed i32 array widened into an i64 array, so `total` can take it: the
// slice type names the element type exactly, and i32 is not i64.
fn fixed_sum(xs: i32[4]) -> i64[4] {
    return [xs[0], xs[1], xs[2], xs[3]];
}
