// Structs of 4096 bytes and more passed by value, several to a call. The C
// passes each by pointer to a copy in the caller's frame, and on arm64
// TinyCC (the in-process backend) addressed every copy 4096 or more bytes
// into that frame from xzr instead of sp: `add xN, xzr, x30`, the
// shifted-register form reading register 31 as zero. The second copy was
// written to a small absolute address. Each struct ends in a growable
// field, as the Goose structs that first hit it did.

struct Page { tag: i64, fill: u8[..4070], tail: u8[>..] }        // 4096 bytes in C
struct Wide { tag: i64, fill: u8[..70000], tail: u8[>..] }

fn page(tag: i64, b: u8) -> Page {
    var p = Page { tag: tag, fill: [], tail: [] };
    p.fill.push(b);
    p.tail.push(b + 1);
    return p;
}

fn wide(tag: i64, b: u8) -> Wide {
    var w = Wide { tag: tag, fill: [], tail: [] };
    w.fill.resize(70000, b);
    w.tail.push(b + 1);
    return w;
}

fn sum(a: const Page&, b: const Page&) -> i64 { a.tag + b.tag + a.fill[0] + b.fill[0] + a.tail[0] + b.tail[0] }

fn two(a: Page, b: Page) -> i64 { sum(a, b) }

fn three(x: i64, a: Wide, b: Page, c: Wide, y: i64) -> i64 {
    x + a.tag + b.tag + c.tag + y + a.fill[69999] + c.fill[0] + a.tail[0] + b.tail[0] + c.tail[0]
}

fn main() {
    print(two(page(1, 10), page(2, 20)));                        // 1 + 2 + 10 + 20 + 11 + 21 = 65
    print(three(100, wide(3, 30), page(4, 40), wide(5, 50), 1000));
    // 100 + 3 + 4 + 5 + 1000 + 30 + 50 + 31 + 41 + 51 = 1315
}
