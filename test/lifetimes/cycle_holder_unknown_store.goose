// A cycle's holder result has the roots its returns give (§7.8), whatever
// the returns call: `make` is an overload set, and its result views static
// data, so the back edge's result may be kept in a global.
struct Fragment { text: const u8[:] }
var kept: Fragment[>..] = [];

fn make(s: const u8[:]) -> Fragment { return Fragment { text: s }; }
fn make(n: i64) -> Fragment { return Fragment { text: "n" }; }

recursive fn f(k: i64) -> Fragment {
    if k > 0 {
        let child = f(k - 1);
        kept.push(child);
    }
    return make("leaf");
}

fn main() {
    print(f(2).text);
    print(kept.len);
}
