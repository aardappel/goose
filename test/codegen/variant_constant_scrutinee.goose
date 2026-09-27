// A payload-less variant constant folded out of an inlined variable-mode
// result is what the tag dispatch and the match on that result inspect
// (-O2 inlines `pick` and `log` into each call site); one of an ADT with a
// resizable payload is matched on a temporary of its own.
enum Msg { Ping, Text { n: i64 } }
enum Log { Empty, Lines { items: i64[>..] } }
fn describe(p: Msg.Ping) -> i64 { 0 }
fn describe(t: Msg.Text) -> i64 { t.n }
fn pick(f: bool) -> Msg.. { if f { Msg.Text { n: 3 } } else { Msg.Ping } }
fn log(f: bool) -> Log.. { if f { Log.Lines { items: [4, 5] } } else { Log.Empty } }
fn main() {
    print(describe(pick(true)), " ", describe(pick(false)));
    print(match pick(false) { Ping => "ping", Text => "text" });
    print(match log(false) { Empty => "empty", Lines => "lines" });
    print(match log(true) { Empty => "empty", Lines => "lines" });
}
