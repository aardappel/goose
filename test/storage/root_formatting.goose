// runtime-debug: formatting inspects owners and components by reference.
root struct State { first: i64[>..], second: u8[>..] }
root struct Custom { values: i64[>..], count: i64 }
fn format(out: u8[>..]&, s: Custom&) { format(out, "count=", s.count, " values=", s.values); }
fn show(s: State&) { print(s); }
fn main() {
    var s = State { [1, 2], "ok" };
    print(s);
    show(s);
    var custom = Custom { [3], 1 };
    print(custom);
}
