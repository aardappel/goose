// A holder passed by value holds what the argument held, as a holder
// variable bound from it would (§9.5), and so does one handed back as it
// was passed. Stored on through a helper, or returned and stored, the rows
// below hold views out of `files`, which point at static data, and nothing
// that points into `other` or `buf`. Each call maps the helper's stores for
// its own arguments, so `second`, checked first, does not change what
// `first` is judged by.
import std;

struct Row { name: const u8[:] }
struct Input { name: const u8[:] }

fn put(rows: Row[>..]&, x: Row) { rows.push(x); }

fn put_copy(rows: Row[>..]&, x: Row) {
    var z = Row { x.name };
    let w = z;
    rows.push(w);
}

fn pass(x: Row) -> Row { return x; }

fn import_files(rows: Row[>..]&, files: const Input[:]) {
    for f in files { put(rows, Row { f.name }); }
    for f in files { put_copy(rows, Row { f.name }); }
    for f in files { rows.push(pass(Row { f.name })); }
}

fn first() {
    var rows: Row[>..] = [];
    var files: Input[>..] = [];
    files.push(Input { "a.csv" });
    import_files(rows, files);
    var n = 0;
    if n == 0 {
        var other: i64[>..] = [];
        other.push(1);
        other.clear();
    }
    print(rows.len, " ", rows[0].name);
}

fn second() {
    var n = 0;
    if n == 0 {
        var buf: u8[>..] = [];
        buf.push('b');
        var rows: Row[>..] = [];
        var files: Input[>..] = [];
        files.push(Input { buf[..] });
        import_files(rows, files);
        print(rows.len, " ", rows[1].name);
    }
}

fn main() {
    second();
    first();
}
