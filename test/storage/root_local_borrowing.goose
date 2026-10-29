// runtime-debug: component provenance survives calls and packed field paths.
struct Tail { flag: bool, values: i64[>..] }
root struct State { first: Tail, second: i64[>..<] }

fn head(a: i64[>..]&) -> i64& { a[0] }
fn clear(a: i64[>..<]&) { a.clear(); }

fn main() {
    var s = State { Tail { true, [10, 20] }, [30] };
    let r .= head(s.first.values);
    clear(s.second);
    s.second.push(40);
    s.first.values.push(50);
    print(r, " ", s.first.values.index_of(r), " ", s.second[0]);
    var image: u8[>..] = s.first.values.to_bytes();
    let restored, ok = from_bytes<i64[>..]>(image);
    print(ok, " ", restored.len, " ", restored[2]);
}
