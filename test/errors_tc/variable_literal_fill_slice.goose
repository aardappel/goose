// error: an array literal of variable-size elements is a Txt[]
// The fill form is no different: its variable-size elements make it a T[],
// which no temporary holds for a slice parameter to view (§4.2).
struct Txt { body: u8[] }
fn lens(ts: Txt[:]) -> i64 { var n = 0; for t in ts { n += t.body.len; } n }
fn main() {
    print(lens([Txt { body: "ab" }; 3]));
}
