// The elements append copies out of an array or slice are stored in the
// receiver, so the references they hold must outlive it (§9.2): they do
// here, however short-lived the array or slice they came from. `in pool`
// links copy anywhere (§3.9).
struct Tag { name: u8[:], weight: i64 }
struct PN { key: i64, link: PN&<u32 in P>? }
var P: PN[>..] = [];

fn add_all(dst: (i64&)[>..]&, src: (i64&)[:]) { dst.append(src); }
fn tag(s: u8[:], w: i64) -> Tag[1] { return [Tag { name: s, weight: w }]; }

fn main() {
    let a = 1;
    let b = 2;
    var refs: (i64&)[>..] = [];
    {
        let xs: (i64&)[2] = [a, b];
        refs.append(xs);          // What xs holds: a and b.
        refs.append(xs[1..]);     // Each element as read out of xs.
    }
    let ys: (i64&)[2] = [b, a];
    add_all(refs, ys[..]);
    for r in refs { print(r); }

    var buf: u8[>..] = [];
    buf.append("alpha beta gamma");
    var words: u8[:][>..] = [];
    {
        var parts: u8[:][>..] = [];
        parts.push(buf[0..5]);
        parts.push(buf[6..10]);
        words.append(parts);
    }
    for i in 2 {
        var parts: u8[:][>..] = [];
        parts.push(buf[6..10]);
        parts.push(buf[11..16]);
        words.append(parts[i..]);
    }
    for w in words { print(w); }

    var tags: Tag[>..] = [];
    {
        let ts: Tag[2] = [Tag { name: buf[0..5], weight: 1 }, Tag { name: buf[6..10], weight: 2 }];
        tags.append(ts);
        tags.append(tag(buf[11..16], 3));
    }
    for t in tags { print(t.name, " ", t.weight); }

    P.push(PN { key: 10, link: null });
    P.push(PN { key: 20, link: P[0] });
    var qs: PN[>..] = [];
    {
        var ps: PN[>..] = [];
        ps.push(PN { key: 30, link: P[1] });
        qs.append(ps);
        qs.append(P[..]);
    }
    qs.resize(5, PN { key: 40, link: P[1] });
    for q in qs {
        let l = q.link;
        guard l else { print(q.key, " -> null"); continue; }
        print(q.key, " -> ", l.key);
    }
}
