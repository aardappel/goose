// error: storing a reference that may point into G, which holds a grow-shrink array
// A literal's field is storage wherever the literal lands (§5.2), an argument
// too: this one may be the view of the grow-shrink G one branch gives, which
// show's clear would leave behind in k.
var G: u8[>..<] = "xyz";
struct K { s: const u8[:] }

fn show(k: K) {
    G.clear();
    G.append("pqr");
    print(k.s);
}

fn main() {
    var b: u8[3] = [97, 98, 99];
    for i in 2 {
        let v = if i == 0 { b[..] } else { G[..] };
        show(K { s: v });
    }
}
