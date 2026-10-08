// runtime-debug: a call into a recursive cycle is made with no data stack of
// the calling activation in use (§7.8), so every activation starts its
// stacks at the same index and the program's stack count is static. The
// shapes that are allowed: a non-fixed result returned whole, through
// branches and a block's tail; text delivered through a buffer passed in by
// reference, built in place; fixed-size values and temporaries anywhere, a
// large fixed one included; a non-fixed temporary in a statement of its own
// before the call; scratch passed in from outside and viewed across the
// calls with mark and resize; format into a buffer passed by reference, a
// recursive overload included.

struct Big { blocks: u8[8192], seed: i64 }
struct Item { value: i64, next: Item? }
recursive fn format(out: u8[>..]&, i: Item&) {
    format(out, i.value);
    let n .= i.next;
    if n { format(out, ",", n); }
}

fn size(s: const u8[:]) -> i64 { s.len }
fn digit(n: i64) -> u8 { return ('0' + n) as u8; }

// Returned whole: the back edge's result is built in the caller's
// destination, as the branches of an if and a block's tail are.
recursive fn chain(n: i64) -> u8[>..] {
    if n == 0 { return "end"; }
    if n % 2 == 0 { return chain(n - 1); }
    return { let k = n; if k > 10 { "big" } else { chain(n - 1) } };
}

// Text built in place into the buffer passed in from outside the cycle,
// each level appending its own part around the level below.
recursive fn build(out: u8[>..]&, n: i64) {
    if n == 0 { out.append("."); return; }
    out.push(digit(n));
    build(out, n - 1);
    out.push(digit(n));
}

// Non-fixed temporaries in statements of their own before the call, fixed
// temporaries at the call (a slice of a fixed literal, a large fixed value
// by value and as a local), and a format into the caller's buffer around it.
recursive fn mixed(out: u8[>..]&, s: const i64[:], n: i64, big: Big) -> i64 {
    if n == 0 { return s[0] + big.seed; }
    let width = size(str("level", n));
    format(out, "[", n);
    var mine = big;
    mine.seed += n;
    let below = mixed(out, [n, n + 1], n - 1, mine);
    let text = str(below);
    format(out, text, "]");
    return width + below + mine.seed - big.seed;
}

// Scratch owned outside the cycle: each level pushes its part, views it
// across the call back into the cycle, and shrinks back on the way out.
recursive fn walk(scratch: i64[>..<]&, depth: i64) -> i64 {
    let mark = scratch.len;
    scratch.push(depth);
    let mine = scratch[mark..];
    var total = 0;
    if depth < 3 { total = walk(scratch, depth + 1); }
    total += mine[0];
    scratch.resize(mark);
    return total;
}

// A recursive format overload renders through the buffer it is given; the
// local buffer's scope ends before the call back into the cycle.
recursive fn count(items: Item&, n: i64) -> i64 {
    if n == 0 { return 0; }
    var len = 0;
    {
        var buf: u8[>..] = [];
        format(buf, items);
        len = buf.len;
    }
    return len + count(items, n - 1);
}

fn main() {
    print(chain(5), " ", chain(4));
    var out: u8[>..] = [];
    build(out, 3);
    print(out);
    var text: u8[>..] = [];
    let big = Big { seed: 7, .. };
    print(mixed(text, [1, 2], 3, big), " ", text);
    var scratch: i64[>..<] = [];
    print(walk(scratch, 0), " ", scratch.len);
    var c = Item { value: 3, next: null };
    var b = Item { value: 2, next: c };
    let a = Item { value: 1, next: b };
    print(count(a, 2));
}
