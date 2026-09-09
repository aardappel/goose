// A fat reference (C.2) held inside another value -- a struct field, an ADT
// payload, an array element, a captured variable, a global -- carries its
// stack without the value's name saying which stack that is. A frame that
// keeps a stack top cached in a local must write it back before a call that
// is handed such a value and read it again after: the callee pushes through
// the header's real top, and the caller's next push has to land above that.
struct Buf { bytes: u8[>..]& }
enum Slot { Empty, Full { buf: Buf } }

fn bump_ref(b: Buf&) { b.bytes.push('!'); b.bytes[0] = 'H'; }
fn bump_val(b: Buf) { b.bytes.push('?'); }
fn bump_first(bs: Buf[>..]&) { bs[0].bytes.push('#'); }
fn bump_slot(s: Slot) { match s { Full f => { f.buf.bytes.push('$'); }, Empty => {} } }

var gtext: u8[>..] = "global";
var gbuf = Buf { bytes: gtext };
fn bump_global() { gbuf.bytes.push('+'); }

fn main() {
    // Growth outside every loop caches the frame's stack tops for the whole
    // body, so every call below runs against cached tops.
    var grown: u8[>..] = "abc";
    grown[1] = 'Y';
    print(grown);                  // aYc
    var text: u8[>..] = "hello";
    var b = Buf { bytes: text };
    bump_ref(b);
    print(text);                   // Hello!
    bump_val(b);
    print(text);                   // Hello!?
    var bs: Buf[>..] = [];
    bs.push(b);
    bump_first(bs);
    print(text);                   // Hello!?#
    bump_slot(Slot.Full { buf: b });
    print(text);                   // Hello!?#$
    fn bump_captured() { b.bytes.push('%'); }
    bump_captured();
    print(text);                   // Hello!?#$%
    // The caller's own push must land above what the callees pushed.
    text.push('.');
    print(text);                   // Hello!?#$%.
    gtext.push('s');
    bump_global();
    gtext.push('.');
    print(gtext);                  // globals+.
}
