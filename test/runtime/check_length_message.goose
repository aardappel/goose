// runtime-debug: a rejected count aborts with the message it was given.
fn main() {
    var a: i64[>..] = [];
    print(a.check_length(3, "reserve: bad count"));
    print(a.check_length(-1, "reserve: bad count"));
}
