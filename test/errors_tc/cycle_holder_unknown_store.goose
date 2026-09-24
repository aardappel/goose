// error: storing the result of a recursive call whose returned reference's root the cycle's returns do not determine
// A holder result's back edge checked before any return has nothing to be
// given: the scan does not resolve the overload set `make` (§7.8), so the
// result carries the root that outlives nothing, which may be passed down
// but not kept.
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
