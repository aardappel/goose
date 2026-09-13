// Codegen summaries follow print's implicit work: a format overload touches a
// global, and the text itself is assembled on a data stack.
struct P { v: i64, w: i64 }
struct Q { v: i64 }
var glog: u8[>..] = [];
fn format(out: u8[>..]&, q: Q) {
    glog.push('F');
    format(out, "Q", q.v);
}
fn show(p: P) { print(p); }
fn showq(q: Q) { print(q); }
fn main() {
    var s: u8[>..] = [];
    s.push('x');
    show(P { v: 1, w: 2 });
    s.push('y');
    print(s);
    glog.push('a');
    showq(Q { v: 1 });
    glog.push('b');
    showq(Q { v: 2 });
    glog.push('c');
    print(glog);
}
