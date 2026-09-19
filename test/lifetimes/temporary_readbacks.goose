// A literal or a call result is a temporary, and a view of it is rooted
// there, but what it holds is not: a slice read out of one points where the
// literal's elements or the callee's result point, here static data, so it
// can be kept (§9.5).
struct Named { name: const u8[:] }
enum Item { Tagged { name: const u8[:] }, Blank }
struct Nums { xs: const i64[:] }

var keep: const u8[:][>..] = [];
var names: Named[>..] = [];
var items: Item[>..] = [];

fn words() -> const u8[:][2] { ["ef", "gh"] }
fn named() -> Named { Named { name: "ij" } }
fn tagged() -> Item { Item.Tagged { name: "kl" } }
fn trio() -> i64[3] { [4, 5, 6] }
fn put(n: Named) { names.push(n); }

// A temporary passed down outlives the call, so the callee may keep it, or
// what a value parameter holds of it, in its own locals.
fn count(xs: const i64[:]) -> i64 {
    let view: const i64[:] = xs;
    var views: const i64[:][>..] = [];
    views.push(xs);
    views.push(view);
    views.len + view.len
}
fn hold(nums: Nums) -> i64 {
    var held: Nums[>..] = [];
    held.push(nums);
    held[0].xs.len
}

fn main() {
    // Read out of literals.
    for w in ["a", "bc"] { keep.push(w); }
    keep.push(["cd", "de"][1]);
    keep.push(Named { name: "fg" }.name);
    for n in [Named { name: "mn" }, Named { name: "op" }] { names.push(n); }
    put([Named { name: "qr" }][0]);
    // Read out of call results.
    for w in words() { keep.push(w); }
    keep.push(words()[1]);
    keep.push(named().name);
    match tagged() {
        Tagged t => items.push(t),
        Blank => {}
    }
    print(keep.len, " ", names.len, " ", items.len);
    for w in keep { print(w); }
    for n in names { print(n.name); }
    for i in items {
        match i {
            Tagged t => print(t.name),
            Blank => {}
        }
    }
    print(count([1, 2, 3]), " ", count(trio()), " ", hold(Nums { xs: [7, 8][..] }));
}
