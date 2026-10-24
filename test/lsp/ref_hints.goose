// lsp: fixture for test/lsp_protocol.py: the `ref` hint before an
// initializer an un-annotated declaration binds by reference (§4.1).
struct Rec { name: u8[], n: i64 }

fn main() {
    let ints = [1, 2, 3];
    let ints2: i64[] = [1, 2, 3];
    var buf: u8[>..] = [];
    var recs: Rec[>..] = [];
    recs.push(Rec { name: "a", n: 1 });
    let x = ints;
    let y = ints2;
    let z = copy(ints2);
    let w .= ints2;
    let b = buf;
    print(x, y, z, w, b);
    for r in recs {
        let name = r.name;
        let n = r.n;
        print(name, n);
    }
}

// lsp: hints 11 1
// lsp: hint 12:13 "ref"
// lsp: hint 12:10 ": i64[]&"
// lsp: hints 12 2
// lsp: hints 13 1
// lsp: hints 14 1
// lsp: hint 15:13 "ref"
// lsp: hints 15 2
// lsp: hint 18:20 "ref"
// lsp: hints 18 2
// lsp: hints 19 1
