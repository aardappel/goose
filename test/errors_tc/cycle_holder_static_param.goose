// error: returning the result of a recursive call whose returned reference's root the cycle's returns do not determine
// The entry call gives `h` static data, which the body takes it for: its
// return gives no root a back edge could map. The back edge gives `h` a
// view of this activation's `local` instead, so what it returns may hold
// that view: its result may only be passed down (§7.8).
struct Fragment { text: const u8[:] }

recursive fn pass(h: Fragment, n: i64) -> Fragment {
    if n == 0 { return h; }
    let local: u8[3] = [97, 98, 99];
    let r = pass(Fragment { text: local[..] }, n - 1);
    return r;
}

fn main() {
    print(pass(Fragment { text: "x" }, 2).text);
}
