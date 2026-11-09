// parse-only: the `own` qualifier and `trusted fn` (docs/design/ownership.md)
// print back as written. `own` binds as `const` does, to the first reference
// or slice built on the base type, and comes before it; both are names
// everywhere else.
struct Node { v: i64, next: own Node?, kids: own Node[:] }
struct Window { id: u64 }
type Handle = Node&;
type Run = own u8[:];

trusted fn make(n: Node&) -> own Node& { return n; }
export trusted fn noop() {}
trusted inline fn alias(n: Node&) -> own Handle { return n; }
trusted checkfree fn checked(n: Node&) -> own Node? { return n; }
trusted recursive fn again(n: i64) -> i64 { if n == 0 { 0 } else { again(n - 1) } }

fn takes(h: own Node&, w: own Window, raw: own u64, s: own const u8[:], hs: (own Node&)[>..<]&,
         o: own Node?, r: Run) {}

fn own(x: i64) -> i64 { x + 1 }
fn trusted(x: i64) -> i64 { own(x) }

fn main() {
    let own = 3;
    let trusted = own;
    print(trusted);
}
