// A local array of own-holding elements must hold none where its scope
// ends (docs/design/ownership.md 4.6): no program text there says what
// becomes of each element, so the one check ownership adds at run time is a
// length compare at the exit, which aborts naming the declaration.
struct Buf { n: i64 }
struct Task { id: i64, buf: own Buf? }

fn fill(n: i64) {
    var ts: Task[>..<] = [];
    for i in n { ts.push(Task { id: i }); }
    print("leaving with ", ts.len);
}

fn main() {
    fill(0);
    fill(2);
    print("not reached");
}
