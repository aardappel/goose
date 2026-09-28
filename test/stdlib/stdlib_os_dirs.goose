// stdlib/os.goose's directories, inside a scratch directory the test makes
// and removes: make_dir, is_dir, delete_dir and list_dir, whose listings
// come sorted bytewise whatever the file system's own order, and a file
// named in scripts no single code page holds, opened by its listed name.
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

fn main() {
    let dir = "gs_os_dirs_tmp";
    clear(dir);
    print(is_dir(dir), " ", make_dir(dir), " ", make_dir(dir), " ", is_dir(dir));   // false true true true
    print(file_exists(dir));                                            // false: not a file
    show(dir);                                                          // list true:

    // "Z" sorts before "a" bytewise, after it in NTFS's case-blind order;
    // the name starting with omega sorts last.
    let name = "ωmega-日本-🐟.txt";
    print(write_file(at(dir, "a.txt"), "apple"), " ", write_file(at(dir, "Z.txt"), "zebra"), " ",
          write_file(at(dir, name), "fish"), " ", make_dir(at(dir, "sub")));  // true true true true
    show(dir);                                         // Z.txt a.txt sub ωmega-日本-🐟.txt

    // A newline can be part of a POSIX file name, never of a Windows one;
    // either way the listing, one name a line, leaves it out.
    let odd = at(dir, "odd\nname");
    write_file(odd, "odd");
    show(dir);                                         // Z.txt a.txt sub ωmega-日本-🐟.txt
    delete_file(odd);

    // The listed name opens the file.
    var names: u8[>..] = [];
    list_dir(dir, names);
    let parts = split(names, '\n');
    var text: u8[>..] = [];
    print(parts.len, " ", read_file(at(dir, parts[3]), text), " ", text, " ",
          parts[3] == name, " ", file_exists(at(dir, name)));             // 5 true fish true true
    print(is_dir(at(dir, "sub")), " ", is_dir(at(dir, "a.txt")), " ",
          file_exists(at(dir, "sub")));                                   // true false false
    show(at(dir, "sub"));                                                 // list true:

    // A listing appends; a failure leaves the builder as it was.
    list_dir(dir, names);
    print(split(names, '\n').len);                                        // 9
    var kept: u8[>..] = [];
    format(kept, "kept");
    print(list_dir(at(dir, "missing"), kept), " ", list_dir(at(dir, "a.txt"), kept), " ",
          list_dir("", kept), " ", kept);                                 // false false false kept
    print(make_dir(at(dir, "a.txt")), " ", make_dir(at(dir, "missing/sub")));   // false false
    print(delete_dir(dir), " ", delete_dir(at(dir, "a.txt")), " ",
          delete_file(at(dir, "sub")), " ", delete_dir(at(dir, "missing")));    // false false false false
    print(is_dir(at(dir, "missing")), " ", is_dir(""));                   // false false

    print(delete_file(at(dir, name)), " ", delete_dir(at(dir, "sub")));   // true true
    show(dir);                                                            // Z.txt a.txt
    clear(dir);
    print(is_dir(dir), " ", list_dir(dir, kept), " ", kept);              // false false kept
}
