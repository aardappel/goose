// `import c`: the declarations of a C header, read by the compiler, with
// no Goose written for them (docs/reference.md, "import c"). The header is
// beside this file; its functions are in the sidecar's C.
import std;
import ffi;
import c "pkg_c_import.h";

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

    // A void pointer to bytes Goose owns; an optional pointer, given or null.
    let b: u8[4] = [1, 2, 3, 4];
    var n: i32 = 0;
    print(ci_sum_bytes(b[0], 4), " ", ci_maybe(n), " ", n, " ", ci_maybe(null));
    print(ci_call_count());
}
