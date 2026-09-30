// `const` in a variable's type makes its contents read-only, not the
// variable (§9.5): a `var` of a const type is assigned as a whole,
// compound-assigned and incremented as its binding allows (§4.4), while its
// elements stay read-only through it. A `const u8[:]` cursor re-slices
// itself over a string literal. A field or an element of a const type is
// assigned as a whole along its path, and read through `&` of it, a slice
// of it and by reference (errors_tc/const_field_* write through those).
struct Named { name: const u8[:] }
struct Tally { counts: const i64[3] }

var cursor: const u8[:] = "global";
var level: const i64 = 1;
var table: const i64[3] = [1, 2, 3];

// A var parameter is a binding of the callee's own.
fn skip(var s: const u8[:], c: u8) -> const u8[:] {
    while s.len > 0 && s[0] == c { s = s[1..]; }
    return s;
}

fn count_words(text: const u8[:]) -> i64 {
    var s = text;
    var words = 0;
    var inword = false;
    while s.len > 0 {
        let space = s[0] == ' ';
        if !space && !inword { words++; }
        inword = !space;
        s = s[1..];
    }
    return words;
}

fn advance_globals() {
    cursor = cursor[2..];
    level += 10;
    level++;
    table = [4, 5, 6];
}

fn main_const_var_assign() {
    var view: const u8[:] = "abc";
    view = "def";
    print(view);
    view = view[1..];
    print(view, " ", view.len);
    var later: const u8[:];
    later = "first";
    later = later[3..];
    var sum = 0;
    var t = "hello";
    while t.len > 0 {
        sum += t[0];
        t = t[1..];
    }
    print(later, " ", sum, " ", t.len);
    let a: const i64[3] = [1, 2, 3];
    var s = a[..];
    s = a[1..];
    print(s, " ", s[0]);
    var arr: const i64[3] = [1, 2, 3];
    arr = [4, 5, 6];
    var xs: const i64[>..] = [1, 2];
    xs = [3, 4, 5];
    var gs: const i64[>..<] = [7, 8, 9];
    gs = [1];
    var lim: const i64[..4] = [1, 2];
    lim = [3, 4, 5];
    print(arr, " ", xs, " ", gs, " ", lim, " ", lim.len);
    var n: const i64 = 1;
    n += 4;
    n *= 3;
    n++;
    n--;
    n--;
    print(n);
    print(skip("   x", ' '), " ", count_words("a text  with four words"));
    advance_globals();
    print(cursor, " ", level, " ", table);
    // A field or element of a const type is assigned whole as well, and a
    // const reference is rebound with .=.
    var named = Named { name: "abc" };
    named.name = "def";
    var views: const u8[:][>..] = ["x"];
    views[0] = "y";
    var x = 1;
    var y = 2;
    var r: const i64& = x;
    r .= y;
    print(named.name, " ", views, " ", r);
    var t = Tally { counts: [1, 2, 3] };
    t.counts = [4, 5, 6];
    var rows: (const i64[3])[2] = [[1, 2, 3], [4, 5, 6]];
    rows[1] = [7, 8, 9];
    let tr = &t.counts;
    var total = 0;
    for &c in t.counts { total += c; }
    print(tr[2], " ", t.counts[1..], " ", total, " ", first(t.counts), " ", rows[1], " ",
          &rows[1] .== &rows[1]);
}

fn first(c: i64[3]&) -> i64 { return c[0]; }

fn main() { main_const_var_assign(); }
