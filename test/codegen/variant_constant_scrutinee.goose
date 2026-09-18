// A payload-less variant constant folded out of an inlined variable-mode
// result is what the tag dispatch and the match on that result inspect
// (-O2 inlines `pick` into each call site).
enum Msg { Ping, Text { n: i64 } }
fn describe(p: Msg.Ping) -> i64 { 0 }
fn describe(t: Msg.Text) -> i64 { t.n }
fn pick(f: bool) -> Msg.. { if f { Msg.Text { n: 3 } } else { Msg.Ping } }
fn main() {
    print(describe(pick(true)), " ", describe(pick(false)));
    print(match pick(false) { Ping => "ping", Text => "text" });
}
