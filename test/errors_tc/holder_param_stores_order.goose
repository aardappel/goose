// error: cannot clear other while rows is still used: a reference into it was stored there
// The rows hold a view of `other` through a helper storing its holder
// parameter. Another caller of the same helper, checked first with views
// of an array it keeps, does not leave the helper's stores mapped for its
// own arguments when `first` calls it (§5.1).
import std;

struct Row { v: i64[:] }
struct Input { v: i64[:] }

fn put(rows: Row[>..]&, x: Row) { rows.push(x); }

fn import_files(rows: Row[>..]&, files: const Input[:]) {
    for f in files { put(rows, Row { f.v }); }
}

fn second() {
    var keep: i64[>..] = [];
    keep.push(2);
    var rows: Row[>..] = [];
    var files: Input[>..] = [];
    files.push(Input { keep[..] });
    import_files(rows, files);
    print(rows.len);
}

fn first() {
    var n = 0;
    if n == 0 {
        var other: i64[>..] = [];
        other.push(1);
        var rows: Row[>..] = [];
        var files: Input[>..] = [];
        files.push(Input { other[..] });
        import_files(rows, files);
        other.clear();
        print(rows[0].v[0]);
    }
}

fn main() {
    second();
    first();
}
