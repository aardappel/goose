// Pops that pass: a grow-shrink array popped later in an expression than an
// operand that read it, where the operand is a copy, a scalar, a fixed
// aggregate or a fresh array the statement made; a pop of an array other than
// the one a live operand or receiver views; a callee that pops after its
// reference argument's last use; a resizable receiver popped into its own
// push, its header alive while its contents change; and a pop after a
// completed call, whose reference argument is no longer live.
struct Copies { first: i64[], second: i64 }
fn use(xs: i64[], n: i64) { print(xs, " ", n); }
fn copies(xs: i64[>..<]&) -> i64[], i64 { return copy(xs), xs.pop(); }
fn owned_copies() {
    var xs: i64[>..<] = [41, 42];
    use(copy(xs), xs.pop());
    print(Copies { copy(xs), xs.pop() });
    xs.push(43);
    let a, n = copies(xs);
    print(a, " ", n);
}

fn make() -> i64[2] { return [1, 2]; }
fn fresh_receivers() {
    var indices: i64[>..<] = [0];
    print(make()[indices.pop()]);
    var lengths: i64[>..<] = [1];
    print(make()[..lengths.pop()]);
    var xs: i64[>..<] = [0];
    var other: i64[2] = [1, 2];
    fn replace() -> i64[:] { xs.pop(); return other[..]; }
    print(make() == replace());
    // A resizable receiver's header remains alive while its contents change.
    var items: i64[>..<] = [3, 4];
    items.push(items.pop());
    print(items);
}

fn sum(x: i64, y: i64) -> i64 { x + y }
fn read(r: i64&, unused: i64) { print(r); }
fn consume(r: i64&, a: i64[>..<]&) { print(r); a.pop(); }
struct Holder { r: i64& }
fn read_holder(value: Holder, unused: i64) { print(value.r); }
fn argument_values() {
    var a: i64[>..<] = [42, 7];
    // The earlier scalar is copied; its source may then be removed.
    print(sum(a[1], a.pop()));
    var other: i64[>..<] = [9];
    read(a[0], other.pop());
    // A reference used by a completed call does not stay live afterward.
    a.pop();
    print(a.len);
    // Binary scalar/fixed aggregate operands are values, not live views.
    var scalar: i64[>..<] = [42];
    print(scalar[0] + scalar.pop());
    var rows: i64[2][>..<] = [[1, 2]];
    print(rows[0] == rows.pop());
    // A completed expression releases its temporary view.
    var values: i64[>..<] = [8, 9];
    print(values[..] == [8, 9]);
    values.pop();
    print(values.len);
    // Bounds may shrink unrelated arrays. The receiver's own header is
    // also stable across calls that consume a reference before shrinking.
    var indices: i64[>..<] = [0];
    print((values[..])[indices.pop()]);
    var lengths: i64[>..<] = [1];
    print((values[..])[..lengths.pop()]);
    consume(values[0], values);
    print(values.len);
    var fixed = 27;
    var disposable: i64[>..<] = [0];
    read_holder(Holder { fixed }, disposable.pop());
}

fn main() {
    owned_copies();
    fresh_receivers();
    argument_values();
}
