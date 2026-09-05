// Codegen execution test for the additions made while writing samples/:
// `.=` declarations, `.==`/`.!=`, `<<=`/`>>=`, bitwise operators binding
// tighter than comparisons, equality across array kinds, `while` narrowing
// a rebound optional, integer constants adapting across branches and
// generic parameters, `from` as an ordinary name, and print rendering its
// whole line before writing it.
import std;

struct Node { v: i32, next: Node&<u32>? }
var pool: Node[>..] = [];
struct Player { name: u8[..8], hp: i32 }
struct P { x: i32, y: i32 }

fn mid<T>(a: T, b: T) -> T { (a + b) / 2 }
fn span(from: i64, to: i64) -> i64 { to - from }

fn shout() -> i64 { print("inner"); 7 }

fn main_syntax_additions() {
    // `.=` binds a reference where `=` would copy the pointee.
    var pts: P[>..] = [];
    let p1: P& = pts.push(P { 1, 2 });
    var walk .= p1;
    var copy = p1;                       // a copy of the point
    walk.x = 20;
    copy.x = 99;
    print(p1.x, " ", copy.x);            // 20 99
    let n0: Node& = pool.push(Node { v: 1 });
    let n1: Node& = pool.push(Node { v: 2, next: n0 });
    let x = 5;
    var alias .= x;
    print(alias + 1);                    // 6
    // Identity, not structural equality.
    let same .= n1;
    print(same .== n1, " ", same .!= n0, " ", n0 .== n1);   // true true false
    let opt = n1.next;
    print(opt .== n0, " ", opt .!= null);                   // true true
    print(n0 .== pool[0], " ", pool[1] .!= n1);             // true false (elements by reference)
    // `while` keeps narrowing a variable the body rebinds.
    var cur = pool[1].next;
    var sum = 0;
    var c: Node? = n1;
    while c {
        sum += c.v;
        c .= c.next;
    }
    print(sum);                          // 3
    // Compound shifts, and & binding tighter than !=.
    var bits: u32 = 1;
    bits <<= 5;
    bits >>= 2;
    print(bits, " ", bits & 8 != 0, " ", bits & 4 == 0);   // 8 true true
    // Equality across array kinds: compare as slices.
    var b: u8[>..] = [];
    b.append("gus");
    let p = Player { name: "gus", hp: 3 };
    print(b == "gus", " ", p.name == b, " ", "gus" == p.name, " ", b != "gum");   // true true true true
    // Integer constants adapt to the other branch, and to a generic bound
    // by another argument.
    let flag = b.len > 2;
    let u: u64 = 10;
    let w = if flag { u } else { 0 };
    let m = match b.len { 3 => u, _ => 1 };
    let k: u8 = 100;
    print(w, " ", m, " ", mid(k, 150));  // 10 10 125
    // `from` is only a keyword after return.
    print(span(3, 10));                  // 7
    // print renders its line before writing it: "inner" comes first.
    print("outer ", shout(), " done");
}

fn main() { main_syntax_additions(); }
