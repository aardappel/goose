// stdlib/os.goose's replacing of files, inside a scratch directory the test
// makes and removes: rename_file onto a new name and over a file, and
// write_file_atomic creating and replacing one, leaving no temporary behind;
// the failures, which leave every file as it was: a missing source, a
// directory in the way, a missing directory, and paths the C APIs would
// read short, past 4095 bytes or up to a NUL byte in them.
import os;

// dir/name.
fn at(dir: const u8[:], name: const u8[:]) -> u8[>..] { str(dir, "/", name) }

// Empties and removes dir, whatever an interrupted run left in it.
fn clear(dir: const u8[:]) {
    var names: u8[>..] = [];
    if !list_dir(dir, names) { return; }
    each_split(names, '\n') {
        if it.len > 0 {
            let p = at(dir, it);
            if is_dir(p) { delete_dir(p); } else { delete_file(p); }
        }
    };
    delete_dir(dir);
}

fn show(dir: const u8[:]) {
    var names: u8[>..] = [];
    print("list ", list_dir(dir, names), ":");
    write_stdout(names);
}

// The file's contents in brackets, or "missing".
fn contents(path: const u8[:]) -> u8[>..] {
    var text: u8[>..] = [];
    if !read_file(path, text) { return str("missing"); }
    return str("[", text, "]");
}

fn main() {
    let dir = "gs_os_replace_tmp";
    clear(dir);
    print(make_dir(dir), " ", make_dir(at(dir, "sub")));                      // true true
    let a = at(dir, "a.txt");
    let b = at(dir, "b.txt");
    let c = at(dir, "c.txt");
    print(write_file(a, "apple"), " ", write_file(b, "banana"));             // true true

    // Onto a new name, then over an existing file.
    print(rename_file(a, c), " ", contents(a), " ", contents(c));            // true missing [apple]
    print(rename_file(c, b), " ", contents(c), " ", contents(b));            // true missing [apple]
    print(rename_file(a, b), " ", contents(b));                              // false [apple]
    print(rename_file(b, at(dir, "sub")), " ", contents(b), " ",
          is_dir(at(dir, "sub")));                                           // false [apple] true
    print(rename_file(b, at(dir, "missing/b.txt")), " ", contents(b));       // false [apple]

    // Creating, replacing with longer and shorter contents, and with none.
    let s = at(dir, "save.dat");
    print(write_file_atomic(s, "first"), " ", contents(s));                  // true [first]
    print(write_file_atomic(s, "second, longer"), " ", contents(s));         // true [second, longer]
    print(write_file_atomic(s, "3rd"), " ", contents(s));                    // true [3rd]
    print(write_file_atomic(s, ""), " ", contents(s));                       // true []
    print(write_file_atomic(b, "banana"), " ", contents(b));                 // true [banana]
    print(write_file_atomic(at(dir, "sub"), "x"), " ", is_dir(at(dir, "sub")));   // false true
    print(write_file_atomic(at(dir, "missing/x.dat"), "x"));                 // false
    show(dir);                                                               // b.txt save.dat sub

    // b.txt's name, run past 4095 bytes by slashes and an x: cut at 4095
    // bytes, it would name b.txt itself.
    var long: u8[>..] = [];
    format(long, dir);
    while long.len < 4090 { long.push('/'); }
    format(long, "b.txt", "x");
    print(long.len, " ", contents(long), " ", file_exists(long), " ", is_dir(long));   // 4096 missing false false
    print(write_file(long, "!"), " ", append_file(long, "!"), " ",
          write_file_atomic(long, "!"), " ", rename_file(long, c), " ",
          rename_file(b, long), " ", delete_file(long), " ", make_dir(long), " ",
          delete_dir(long));                                  // false false false false false false false false
    var kept: u8[>..] = [];
    print(list_dir(long, kept), " ", kept.len, " ", contents(b), " ", contents(c));  // false 0 [banana] missing

    // b.txt's name with a NUL and an x after it.
    var nul: u8[>..] = [];
    format(nul, b);
    nul.push(0);
    format(nul, "x");
    print(nul.len, " ", contents(nul), " ", file_exists(nul), " ", is_dir(nul));      // 25 missing false false
    print(write_file(nul, "!"), " ", append_file(nul, "!"), " ",
          write_file_atomic(nul, "!"), " ", rename_file(nul, c), " ",
          rename_file(b, nul), " ", delete_file(nul), " ", make_dir(nul), " ",
          delete_dir(nul));                                   // false false false false false false false false
    print(list_dir(nul, kept), " ", kept.len, " ", contents(b), " ", contents(c));   // false 0 [banana] missing
    show(dir);                                                               // b.txt save.dat sub

    clear(dir);
    print(is_dir(dir));                                                      // false
}
