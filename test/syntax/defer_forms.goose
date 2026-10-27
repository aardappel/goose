// The forms of defer (§6.6): a statement, with the statements that start
// with an expression, or a block, with or without a trailing `;`.
fn pair() -> i64, i64 { return 1, 2; }
fn count(n: i64&) { n++; }

fn main() {
    var n = 0;
    var a = 0;
    var b = 0;
    defer print("n ", n, " a ", a, " b ", b);
    defer n++;
    defer n += 10;
    defer a, b = pair();
    defer count(n);
    defer if n > 0 { print("positive") } else { print("not positive") }
    defer { print("block"); n = 100; };
    defer {
        print("multi");
        print("line");
    }
    defer {}
    defer {};
    defer {
        n
    }
    defer for i in 2 { print("for ", i); }
    print("body");
}
