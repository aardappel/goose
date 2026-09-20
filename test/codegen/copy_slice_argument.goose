// runtime-debug: copy(x) passed where a slice is expected is a fresh copy of
// x (§4.1, §4.2), sliced whole (§3.10). The callee's view keeps the value x
// had when the argument was evaluated, whatever happens to x after: the
// callee writing it, a later argument writing it, or x, an element, popped
// and its slot pushed over. Through each call form, from fixed, limited,
// variable and resizable arrays.
struct Holder { arr: i64[3] }
enum Shape { Circle { r: i64 }, Point { k: i64 } }

var g: i64[3] = [1, 2, 3];
var h = Holder { arr: [4, 5, 6] };
var lim: i64[..4] = [7, 8];
var text: u8[] = "abc";
var word: u8[>..] = "hey";
var rows: i64[3][>..<] = [[1, 2, 3], [4, 5, 6]];
var letter: u8[>..] = "a";
var spelled: u8[>..] = "";

// Each writes its argument's source, then reads the argument.
fn head(s: const i64[:]) -> i64 { g[0] = 0; s[0] }
fn second<T>(s: const T[:]) -> T { g[1] = 0; s[1] }
fn third(c: Shape.Circle, s: const i64[:]) -> i64 { g[2] = 0; s[2] + c.r }
fn third(p: Shape.Point, s: const i64[:]) -> i64 { g[2] = 0; s[2] * p.k }
fn apply<F>(n: i64) -> i64 { F(copy(g)) + n }
fn pointee(r: i64[3]&) -> i64 { head(copy(r)) }
fn field(s: const i64[:]) -> i64 { h.arr[0] = 0; s[0] }
fn limited(s: const i64[:]) -> i64 { lim[0] = 0; s[0] }
fn variable(s: const u8[:]) -> const u8[:] { text[0] = '-'; s }
fn resizable(s: const u8[:]) -> const u8[:] { word[0] = '-'; word.push('!'); s }
fn popped(s: const i64[:]) -> i64 { rows.pop(); rows.push([0, 0, 0]); s[0] }
fn plus(s: const i64[:], n: i64) -> i64 { s[0] + n }
fn clobber() -> i64 { g[0] = 100; 0 }

// Allocates over whatever a released temporary left behind; returns 0.
fn churn() -> i64 {
    var fill: u8[>..] = "????????????????????????????????";
    fill.len - 32
}

// At -O1 and above the self-call's base case is inlined in its place, with
// the copy bound in front of it.
recursive fn spell(s: const u8[:], n: i64) {
    if n == 0 { return; }
    letter[0] = letter[0] + 1;
    let k = churn();
    spelled.push(s[0] + k as u8);
    spell(copy(letter), n - 1);
}

fn main() {
    print(head(copy(g)), " ", g[0]);
    g = [1, 2, 3];
    print(copy(g).head(), " ", g[0]);
    g = [1, 2, 3];
    print(second(copy(g)), " ", g[1]);
    g = [1, 2, 3];
    let c: Shape = Shape.Circle { r: 10 };
    print(third(c, copy(g)), " ", g[2]);
    g = [1, 2, 3];
    print(apply(0, head), " ", g[0]);
    g = [1, 2, 3];
    print(pointee(g), " ", g[0]);
    g = [1, 2, 3];
    // The later argument runs after the copy is taken: the plain argument
    // sees what it wrote, the copy does not.
    print(plus(copy(g), clobber()), " ", g[0]);
    g = [1, 2, 3];
    print(plus(g, clobber()), " ", g[0]);
    print(field(copy(h.arr)), " ", h.arr[0]);
    print(limited(copy(lim)), " ", lim[0]);
    print(variable(copy(text)), " ", text);
    print(resizable(copy(word)), " ", word);
    print(popped(copy(rows[1])), " ", rows[1][0]);
    spell(copy(letter), 3);
    print(spelled, " ", letter);
}
