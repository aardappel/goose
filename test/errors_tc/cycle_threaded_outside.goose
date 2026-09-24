// error: references may only be passed down, not stored, inside a recursive cycle
// walk passes `c` on at every call back into its cycle, so in every
// activation it points where the call that created walk's specialization
// said. That is only storable inside the cycle if it is rooted outside any
// cycle (§7.8), as a free variable's owner must be: here it is a local of
// the recursive visit, whose activations each have one.
struct Ctx { a: i64, link: i64&? }

recursive fn walk(c: Ctx&, n: i64) {
    c.link .= c.a;
    if n > 0 { walk(c, n - 1); }
}

recursive fn visit(n: i64) -> i64 {
    var c = Ctx { a: n, link: null };
    walk(c, 2);
    if n > 0 { return visit(n - 1) + c.a; }
    return c.a;
}

fn main() { print(visit(3)); }
