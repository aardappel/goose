// The right-hand side of a whole assignment of a struct's tail field reads
// the struct's other fields, references among them, where those cannot lead
// back to the tail being rebuilt: a reference to storage of another type, or
// to another struct whose own tail has the tail's type (§4.4). Through a
// reference parameter, in a loop, for grow-only and grow-shrink tails.
import std;

struct Cell { x: i32, y: i32 }
struct Pos { y: i32, x: i32 }
struct Walk { start: Cell&, steps: u32, path: Pos[>..] }

fn find(start: Cell&) -> Pos[>..] {
    var cells: (Cell&)[>..] = [];
    cells.push(start);
    var p: Pos[>..] = [];
    p.push(Pos { y: cells[0].y, x: cells[0].x });
    p.push(Pos { y: cells[0].y + 1, x: cells[0].x });
    return p;
}

fn walk(s: Walk&) {
    s.path = find(s.start);
    s.steps += s.path.len as! u32;
}

struct Queue { start: Cell&, steps: u32, path: Pos[>..<] }

fn queue(s: Queue&) {
    let start = s.start;
    s.path = find(start);
    s.steps += s.path.len as! u32;
}

struct Enc { k: i32, data: u8[>..<] }
struct Dec { n: i64, enc: Enc&, tail: u8[>..<] }

fn decode(r: Enc&) -> u8[>..<] {
    var o: u8[>..<] = [];
    o.append(r.data);
    o.push(r.k as u8);
    return o;
}

fn run_dec(s: Dec&) {
    s.tail = decode(s.enc);
    s.n += s.tail.len;
}

struct Src { k: i32, data: u8[>..] }
struct Out { n: i64, src: Src&, tail: u8[>..] }

fn render(r: Src&) -> u8[>..] {
    var o: u8[>..] = [];
    o.append(r.data);
    return o;
}

fn run_out(s: Out&) {
    let src = s.src;
    s.tail = render(src);
    s.n += s.tail.len;
}

fn main() {
    var c = Cell { x: 1, y: 2 };
    var w = Walk { start: c, steps: 0, path: [] };
    for i in 3 { walk(w); }
    print(w.steps, " ", w.path[1].y);

    var q = Queue { start: c, steps: 0, path: [] };
    for i in 2 { queue(q); }
    print(q.steps, " ", q.path.len);

    var e = Enc { k: 99, data: [97, 98] };
    var d = Dec { n: 0, enc: e, tail: [] };
    for i in 3 { run_dec(d); }
    print(d.tail, " ", d.n);

    var src = Src { k: 0, data: [100, 101] };
    var o = Out { n: 0, src: src, tail: [] };
    for i in 2 { run_out(o); }
    print(o.tail, " ", o.n);
}
