// A grow-only array shrinks after the last use of a slice, reference or
// holder into it, without that holder's block having to end (§5.1).
import std;

struct Slot { s: u8[:] }

fn reset(buf: u8[>..]&) { buf.clear(); }

fn main() {
    var line: u8[>..] = [];
    line.append("hello world");
    let w = line[0..5];
    print(w);                            // the last use of w
    line.clear();                        // so this is fine
    line.append("again");
    print(line);
    // A holder value that is never read after the shrink.
    var slots: Slot[>..] = [];
    slots.push(Slot { s: line[0..2] });
    print(slots[0].s);
    line.clear();
    line.append("x");
    print(line);
    // In a loop body: a slice bound and used before the shrink each time.
    var buf: u8[>..] = [];
    for i in 3 {
        let v = buf[..];
        print(v.len);
        buf.clear();
        format(buf, "item", i);
    }
    // Through a helper: the shrink is checked at the call.
    let w3 = buf[..];
    print(w3);
    reset(buf);
    buf.append("done");
    print(buf);
}
