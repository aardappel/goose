// A namespaced file that is also a program (docs/design/namespaces.md): the
// entry point must be the global main, which `fn ::main()` declares from
// inside the namespace. An importer ignores it like any imported main (§11.1).
namespace nslib;

struct Counter { n: i64 }

fn bump(c: Counter&) { c.n += 1; }

fn main_nslib() {
    var c = Counter { 0 };
    c.bump();          // UFCS resolves in nslib first.
    bump(c);
    print(c.n);        // 2
}

// Declared global, so written as global code: the call needs the qualifier.
fn ::main() { nslib::main_nslib(); }
