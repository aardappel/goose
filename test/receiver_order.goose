// A member operation's receiver is evaluated before its arguments, even when
// they rebind the reference naming it or one it is reached through.
struct Holder { id: i64, lim: i64[..4] }
struct Bag { tag: i64, items: i64[>..] }
struct Text { id: i64, buf: u8[>..] }
fn format(out: u8[>..]&, h: Holder) { format(out, "H", h.id); }
var fa: i64[4] = [1, 2, 3, 4];
var fb: i64[4] = [5, 6, 7, 8];
var ba = Bag { tag: 1, items: [10, 20, 30, 40] };
var bb = Bag { tag: 2, items: [50] };
fn main() {
    var a: i64[>..] = [1];
    var b: i64[>..] = [2];
    var r .= a;
    fn change() -> i64[>..] { r .= b; return [8, 9]; }
    r.append(change());
    print(a, " ", b);
    var c: i64[>..] = [1];
    var d: i64[>..] = [2];
    var q .= c;
    fn change_push() -> i64 { q .= d; return 9; }
    q.push(change_push());
    print(c, " ", d);
    var e: u8[>..] = [];
    var f: u8[>..] = [];
    var t .= e;
    fn change_format() -> i64 { t .= f; return 9; }
    t.format(change_format());
    print(e.len, " ", f.len);
    var g: i64[>..<] = [1];
    var h: i64[>..<] = [2];
    var u .= g;
    fn change_resize() -> i64 { u .= h; return 3; }
    u.resize(change_resize(), 0);
    print(g, " ", h);
    var w .= fa;
    fn elem() -> i64& { w .= fb; return fa[2]; }
    print(w.index_of(elem()));
    var h1 = Holder { id: 1, lim: [1] };
    var h2 = Holder { id: 2, lim: [2, 3, 4] };
    var hr .= h1;
    fn change_field() -> i64 { hr .= h2; return 9; }
    hr.lim.push(change_field());
    print(h1.lim, " ", h2.lim);
    var m1: i64[..4][2] = [[1], [2]];
    var m2: i64[..4][2] = [[5, 6, 7], [8]];
    var mr .= m1;
    fn change_element() -> i64 { mr .= m2; return 9; }
    mr[0].push(change_element());
    print(m1[0], " ", m2[0]);
    var bag_a = Bag { tag: 1, items: [1] };
    var bag_b = Bag { tag: 2, items: [2] };
    var br .= bag_a;
    fn change_tail() -> i64[>..] { br .= bag_b; return [8, 9]; }
    br.items.append(change_tail());
    bag_a.items.push(5);
    bag_b.items.push(6);
    print(bag_a.items, " ", bag_b.items);
    var gr .= ba;
    fn tail_elem() -> i64& { gr .= bb; return ba.items[3]; }
    print(gr.items.index_of(tail_elem()));
    var text_a = Text { id: 1, buf: [] };
    var text_b = Text { id: 2, buf: [] };
    var tr .= text_a;
    fn change_text() -> Holder { tr .= text_b; return Holder { id: 7, lim: [1] }; }
    tr.buf.format(change_text());
    print(text_a.buf, " ", text_b.buf.len);
}
