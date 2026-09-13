// `let` keeps a binding from being reassigned and nothing more (§4.4): the
// contents of a let value, and what a reference to it reaches, are as
// writable as the type says. `const` is what makes contents read-only.
struct Pt { x: i64, y: i64 }
struct Rec { let id: i64, pos: Pt, const tags: i64[2] }

fn bump(p: Pt&) { p.x += 10; }
fn fill(xs: i64[:]) { for &x in xs { x = 7; } }

fn main_let_contents() {
    let xs = [1, 2, 3];
    xs[0] = 9;                       // an element of a let array
    fill(xs[..]);                    // a slice of a let is writable
    print(xs);
    let p = Pt { x: 1, y: 2 };
    p.y = 5;                         // a field of a let struct
    bump(p);                         // a reference to a let is writable
    print(p.x, " ", p.y);
    let r = &p;
    r.x = 0;
    print(p.x);
    let rec = Rec { id: 1, pos: Pt { x: 0, y: 0 }, tags: [4, 5] };
    rec.pos.x = 3;                   // a field of a let field's value
    print(rec.id, " ", rec.pos.x, " ", rec.tags);
    const cs = [1, 2, 3];
    let view = cs[..];               // a slice of a const value is a const slice
    print(view[1]);
    var copy: i64[3] = cs;           // a copy of a const value is writable
    copy[1] = 8;
    print(copy);
    var n = 0;
    for x in xs { n += x; }          // a by-value binding reads
    print(n);
}

fn main() { main_let_contents(); }
