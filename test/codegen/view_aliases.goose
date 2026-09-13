// A loop that reads an array through a reference (its length here) must read
// it afresh after anything that can grow or shrink that array through another
// name: the owning variable itself, or a reference held in a struct field or
// an array element, which points wherever it was bound rather than into the
// value holding it -- whether the operation happens in a callee handed the
// holder, or right in the loop body. Every case here also runs inlined into
// main at -O2, where the copied variables' roots must follow the copies.
struct Buf { bytes: u8[>..]& }
struct Shr { bytes: u8[>..<]& }

fn grow(b: Buf&) { b.bytes.push('x'); }
fn drop_last(s: Shr&) { s.bytes.pop(); }

fn via_callee() {
    var text: u8[>..] = "hello";
    var b = Buf { bytes: text };
    var r: u8[>..]& = text;
    var n = 0;
    var i = 0;
    while i < 3 {
        r[i] = 'A' + i as u8;
        grow(b);
        n += r.len;
        i++;
    }
    print(text, " ", n);           // ABCloxxx 21
}

fn via_field() {
    var text: u8[>..] = "hello";
    var b = Buf { bytes: text };
    var r: u8[>..]& = text;
    var n = 0;
    var i = 0;
    while i < 3 {
        r[i] = 'A' + i as u8;
        b.bytes.push('x');
        n += r.len;
        i++;
    }
    print(text, " ", n);           // ABCloxxx 21
}

fn via_element() {
    var text: u8[>..] = "hello";
    var bs: Buf[>..] = [];
    bs.push(Buf { bytes: text });
    var r: u8[>..]& = text;
    var n = 0;
    var i = 0;
    while i < 3 {
        r[i] = 'A' + i as u8;
        bs[0].bytes.push('x');
        n += r.len;
        i++;
    }
    print(text, " ", n);           // ABCloxxx 21
}

fn shrinking() {
    var text: u8[>..<] = "abcdefgh";
    var s = Shr { bytes: text };
    var r: u8[>..<]& = text;
    var seen: u8[>..] = [];
    var i = 0;
    while i < r.len {
        seen.push(r[i]);
        drop_last(s);
        s.bytes.pop();
        i++;
    }
    print(seen, " ", text.len);    // abc 2
}

fn via_owner() {
    var text: u8[>..] = "hello";
    var r: u8[>..]& = text;
    var n = 0;
    var i = 0;
    while i < 3 {
        r[i] = 'A' + i as u8;
        text.push('x');
        n += r.len;
        i++;
    }
    print(text, " ", n);           // ABCloxxx 21
}

fn main() {
    via_owner();
    via_callee();
    via_field();
    via_element();
    shrinking();
}
