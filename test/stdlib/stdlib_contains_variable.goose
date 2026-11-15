// std's contains over arrays of variable-size elements, which can only be
// iterated (spec §3.3), as position iterates them. contains handed its own
// parameter v on to position's by-value v: a non-fixed lvalue, which is
// never copied implicitly (§4.1), so the call failed to check inside std
// wherever position works, and no argument at the call could help.
import std;

struct Named { name: u8[], id: i64 }

fn main() {
    let words: u8[][>..] = ["bb", "a", "ccc"];
    print(position(words, "ccc"), " ", position(words, "zz"));        // 2 -1
    print(contains(words, "ccc"), " ", contains(words, "zz"));        // true false
    print(words.contains("a"), " ", contains(words[..], "b"));        // true false
    let w: u8[] = "bb";
    print(contains(words, copy(w)));                                  // true
    let grid: i64[][>..] = [[1, 2], [3]];
    print(contains(grid, [3]), " ", contains(grid, [2, 1]));          // true false
    let people: Named[>..] = [Named { "ann", 1 }, Named { "bob", 2 }];
    print(contains(people, Named { "bob", 2 }), " ",
          contains(people, Named { "bob", 3 }));                      // true false
    let none: u8[][>..] = [];
    print(contains(none, ""));                                        // false
    // Fixed-size elements are unchanged, references among them, which
    // compare their pointees (§4.5).
    print(contains([3, 1, 4], 4), " ", contains("abc", 'z'));         // true false
    var a = 5;
    var b = 5;
    var c = 6;
    var refs: i64&[>..] = [];
    refs.push(a);
    print(contains(refs, b), " ", contains(refs, c));                 // true false
}
