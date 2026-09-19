// error: an array literal of variable-size elements is a Txt[]
// error: never as a temporary for a slice or reference to view
// A literal of variable-size elements is a T[], which comes into existence
// only in a construction context (§4.2); a slice parameter is not one, where
// a by-value Txt[] parameter is.
struct Txt { body: u8[] }
fn lens(ts: Txt[:]) -> i64 { var n = 0; for t in ts { n += t.body.len; } n }
fn main() {
    print(lens([Txt { body: "hey" }, Txt { body: "x" }]));
}
