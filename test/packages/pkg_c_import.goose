// `import c`: the declarations of a C header, read by the compiler, with
// no Goose written for them (docs/reference.md, "import c"). The header is
// beside this file; its functions are in the sidecar's C.
import std;
import ffi;
import c "pkg_c_import.h";

// C calls it back for each element, with the user data ci_each was given.
var visti: u8[>..] = [];
export fn visita(ud: u64, v: i32) -> i32 {
    // Text built on the data stacks, while main holds one of its own.
    format(visti, "[", ud, ":", str(v, "!"), "]");
    return v * 2;
}

fn main() {
    // Constants: integer, wide, unsigned, floating, string, pointer, enum.
    print(CI_ANSWER, " ", CI_NEG, " ", CI_BIG, " ", CI_MASK, " ", CI_RATIO, " ", CI_NAME);
    print(CI_SENTINEL == 18446744073709551615, " ", CI_RED, " ", CI_GREEN, " ", CI_BLUE);

    // A handle: the struct C never completes, typed and null at first.
    var c = ci_counter_new("conta");
    ci_counter_add(c, 2);
    print(ffi::cstr(ci_counter_name(c)), " ", ci_counter_add(c, 3));
    ci_counter_free(c);
    var d = ci_counter {};
    print(d.p == 0, " ", ci_counter_open("", d), " ", d.p == 0);
    print(ci_counter_open("altro", d), " ", d.p != 0, " ", ffi::cstr(ci_counter_name(d)));
    ci_counter_free(d);

    // C strings: any slice, copied with its NUL, short or long; or null.
    let parola: u8[>..] = "anatra";
    var lunga: u8[>..] = [];
    for i in 0..1000 { lunga.push('x'); }
    print(ci_strlen("goose"), " ", ci_strlen(parola[1..4]), " ", ci_strlen(lunga), " ", ci_strlen(null));

    // Scalars as C has them: long, unsigned char, _Bool, an enum.
    print(ci_long_twice(21), " ", ci_flags(1), " ", ci_is_even(4), " ", ci_next_color(CI_RED));

    // Structs: one C pads, filled through a pointer; one by value.
    var it = ci_item { tag: 0, value: 0, weight: 0.0 };
    ci_item_fill(it, 'k', 4, 2.5);
    print(it.tag, " ", it.value, " ", it.weight, " ", ci_item_weight(it));
    let p = ci_point_add(ci_point { x: 1, y: 2 }, ci_point { x: 10, y: 20 });
    print(p.x, " ", p.y);
    // Padded structs by value, both ways: extern structs, as C lays them out.
    let fatto = ci_item_make('z', 7, 0.5);
    let fd = ci_fd_swap(ci_fd { f: 1.5, d: 2.25 });
    print(fatto.tag, " ", fatto.value, " ", fatto.weight, " ", ci_item_total(fatto), " ", fd.f, " ", fd.d);

    // A void pointer (cptr): an array, a slice, an empty one, a reference,
    // an address held as u64, or null. An optional pointer, given or null.
    let b: u8[4] = [1, 2, 3, 4];
    let q = ci_point { x: 1, y: 2 };
    print(ci_sum_bytes(b, 4), " ", ci_sum_bytes(b[1..3], 2), " ", ci_sum_bytes(b[0..0], 0), " ",
          ci_sum_bytes(&q, 8), " ", ci_addr(12345), " ", ci_addr(null));
    var n: i32 = 0;
    print(ci_maybe(n), " ", n, " ", ci_maybe(null));

    // A pointer and a length: a slice or an array where C wants `int *`,
    // as its elements' address, also when there are none.
    var xs: i32[3] = [1, 2, 3];
    let tutti: i32[>..] = [4, 5, 6, 7];
    print(ci_sum_ints(xs, 3), " ", ci_sum_ints(tutti[1..], 3), " ", ci_sum_ints(tutti[0..0], 0));
    ci_fill(xs, xs.len as u64, 7);
    print(xs[0], " ", xs[1], " ", xs[2]);

    // A callback: cfn(f) of an export fn of the signature C calls, with an
    // integer as its user data; null where C allows none.
    var tenuto: u8[>..] = "prima ";
    let doppi = ci_each(xs, 3, cfn(visita), 5);
    tenuto.append("dopo");
    print(doppi, " ", visti, " ", tenuto, " ", ci_each(xs, 3, null, null));
    print(ci_call_count());
}
