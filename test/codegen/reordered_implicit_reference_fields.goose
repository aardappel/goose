// runtime-debug: reordered implicit references retain their separate owners.
import std;

struct Owner {
    first: i64[>..]&,
    second: i64[>..]&
}

enum Hold {
    Arrays {
        first: i64[>..]&,
        second: i64[>..]&
    }
}

fn main() {
    var first_array: i64[>..] = [];
    var second_array: i64[>..] = [];
    // Initializers deliberately reverse the field declaration order.
    var owner = Owner {
        second: second_array,
        first: first_array
    };
    owner.first.push(7);
    owner.second.push(9);
    assert(first_array.len == 1 && second_array.len == 1);
    assert(first_array[0] == 7 && second_array[0] == 9);

    var variant_first_array: i64[>..] = [];
    var variant_second_array: i64[>..] = [];
    var hold: Hold = Hold.Arrays {
        second: variant_second_array,
        first: variant_first_array
    };
    match hold {
        Arrays arrays => {
            arrays.first.push(11);
            arrays.second.push(13);
        }
    };
    assert(variant_first_array.len == 1 && variant_second_array.len == 1);
    assert(variant_first_array[0] == 11 && variant_second_array[0] == 13);

    print("reordered implicit reference fields passed");
}
