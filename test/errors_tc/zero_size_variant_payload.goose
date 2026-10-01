// error: field v of variant Opt.Some cannot be a variant type with no fields: Mode.Off
// A payload field is a field: a type argument making it a variant type with
// no fields is rejected where the enum is instantiated (§3.4).
enum Mode { Off, Level { v: i64 } }
enum Opt<T> { Some { v: T, n: i64 }, None }

fn main() {
    let o: Opt<Mode.Off> = Opt<Mode.Off>.Some { v: Mode.Off {}, n: 1 };
    match o { Some s => print(s.n), None => print("none") }
}
