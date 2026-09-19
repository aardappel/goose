// A temporary lasts until the end of the statement that made it, so a
// variable of a scope that statement opens -- a `for` body or a `match` arm
// over it -- may keep a view into it. What a temporary holds points where
// whatever built it put it, and outlives the temporary itself.
import std;

struct Txt { body: u8[] }
struct Named { name: const u8[:], n: i64 }
struct View { s: const u8[:] }
enum Shape { Circle { label: const u8[:] }, Square { side: i64 } }

fn mk() -> Txt[] { [Txt { body: "hey" }, Txt { body: "x" }] }
fn mk_words() -> const u8[:][] { ["alpha", "beta", "gamma"] }
fn mk_shape(k: i64) -> Shape {
    if k == 0 { Shape.Circle { label: "round" } } else { Shape.Square { side: k } }
}
fn first_word() -> const u8[:] { mk_words()[0] }
fn pick(ws: const u8[:][:], i: i64) -> const u8[:] { ws[i] }

fn main() {
    var total = 0;
    for t in mk() {
        let b = t.body[..];
        let annotated: const u8[:] = t.body[..];
        var views: View[>..] = [];
        views.push(View { s: b[1..] });
        if b.len > 1 { let inner = b[..1]; total += inner.len; }
        total += b.len + annotated.len + views[0].s.len;
    }
    print(total);
    match mk_shape(0) {
        Circle c => { let l = c.label; print(l); },
        Square s => print(s.side),
    }
    let w = mk_words()[0];
    let line: u8[] = "k1,v1;k2";
    let key = split(line, ',')[0];
    print(w, " ", key, " ", first_word());
    var keep: const u8[:][>..] = [];
    for x in mk_words() { keep.push(x); }
    var stack: Named[>..<] = [Named { name: "one", n: 1 }, Named { name: "two", n: 2 }];
    let top = stack.pop();
    keep.push(top.name);
    print(keep.len, " ", keep[1], " ", keep[3]);
    // The temporary bound to a variable of its own lasts as long as that.
    let ws = mk_words();
    print(pick(ws, 1));
}
