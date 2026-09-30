// error: str cannot render A, which reaches itself through references, around the format overload for Tag: give A a format overload of its own
// The part with an overload may lie at any depth the references of a type
// that reaches itself through them lead to (§3.7): here in B, which A holds
// a reference to, in an ADT's payload.
struct Tag { id: i64 }
fn format(out: u8[>..]&, t: Tag&) { format(out, "#", t.id); }
enum Mark { None, Tagged { tag: Tag } }
struct A { x: i64, b: B? }
struct B { mark: Mark, a: A? }
fn main() {
    var a0 = A { x: 0, b: null };
    var b1 = B { mark: Mark.Tagged { tag: Tag { id: 7 } }, a: a0 };
    let a2 = A { x: 2, b: b1 };
    let o: A? = a2;
    print(str(o));
}
