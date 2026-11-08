// A generic `format` overload whose value parameter is a struct or ADT
// pattern renders every instance of it in print, str and format (§3.7); a
// non-generic overload for one instance takes that one. One on a bare type
// variable or an array of one is an ordinary overload, not a rendering hook.
struct Type<T> { _: bool = false }
struct Box<T> { v: T }
struct Big<T> { v: T, s: u8[>..] }
enum Opt<T> { Some { v: T }, None }

fn sizeof(t: Type<bool>) -> u32 { return 1; }
fn sizeof(t: Type<u32>) -> u32 { return 4; }

fn format<T>(out: u8[>..]&, t: Type<T>) {
    format(out, "type with size=", sizeof(t));
}

fn format<T>(out: u8[>..]&, b: Box<T>) { format(out, "Box(", b.v, ")"); }
fn format(out: u8[>..]&, b: Box<bool>) { out.append("BoolBox"); }
fn format<T>(out: u8[>..]&, b: Big<T>&) { format(out, "Big ", b.v, " of ", b.s.len); }
fn format<T>(out: u8[>..]&, o: const Opt<T>&) {
    match o {
        Some s => format(out, "Some ", s.v),
        None => out.append("nothing"),
    }
}

fn show<T>(v: T) { print(v); }

fn main() {
    print(Type<u32> {});
    let out: u8[>..] = [];
    format(out, Type<u32> {});
    print(out);
    print(Box<i32> { 3 }, " ", Box<bool> { true }, " ", Box<Box<f64>> { Box<f64> { 1.5 } });
    print([Box<i32> { 1 }, Box<i32> { 2 }], " ", str(Box<u8> { 7 }).len);
    var b = Big<i64> { 5, [] };
    b.s.push(1);
    print(b);
    let some: Opt<i32> = Opt<i32>.Some { 4 };
    let none: Opt<i32> = Opt<i32>.None {};
    print(some, " ", none);
    show(Box<i16> { 6 });
    let direct: u8[>..] = [];
    format(direct, "x=", Box<i32> { 8 }, " ", 9);
    print(direct);
}
