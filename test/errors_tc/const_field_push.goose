// error: cannot .push through a non-writable value
// Growing a field of a const array type writes its contents (§9.5), which
// assigning it as a whole does not.
struct Log { lines: const i64[>..] }
fn main() {
    var l = Log { lines: [1, 2] };
    l.lines = [3];
    l.lines.push(4);
    print(l.lines);
}
