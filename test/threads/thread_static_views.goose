// A `let` global whose references lead only to static data -- a string
// literal, an array or struct of them -- is read by a worker as its own copy
// (§11.2): the copy views the same read-only bytes, so no program can tell.
// json::format_number reads such a table of digit pairs.
import std;
import json;

let PAIRS = "0001";
const DIGITS = "0123456789";
let NAMES = ["ab", "cd"];
struct Cfg { name: const u8[:], n: i64 }
let CFG = Cfg { name: "cfg", n: 3 };
let EMPTY: const u8[:] = "";
// A null reference leads nowhere, whatever its type reaches: here itself.
struct Node { v: i32, next: Node? }
let HEAD: Node? = null;

thread_fn worker(i: i64) {
    var out: u8[>..] = [];
    json::format_number(out, 0.5);
    print(PAIRS[i], " ", DIGITS[i], " ", NAMES[i], " ", CFG.name, " ", CFG.n, " ", EMPTY.len,
          " ", out);
    if HEAD { print(HEAD.v); } else { print("no head"); }
}

fn main() {
    thread_wait(thread_spawn(worker, 1));
    print(PAIRS[0], " ", DIGITS[0], " ", NAMES[0], " ", CFG.name);
}
