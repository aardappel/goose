// runtime-debug: repeated source names, explicit suffixes and globals share one C namespace.
var value: i64 = 100;

// The overloads keep globally reserved second and third C spellings live.
fn marker(input: i64) -> i64 { return input; }
recursive fn value(input: i32) -> i32 { return input; }
recursive fn value(input: i16) -> i16 { return input; }

recursive fn first(input: i64) {
    var value: i64[>..] = [input];
    print(value[0]);
    {
        var value_2: i64[>..] = [input + 1];
        print(value_2[0]);
    }
    {
        var value: i64[>..] = [input + 2];
        print(value[0]);
    }
    {
        var value_2: i64[>..] = [input + 3];
        print(value_2[0]);
    }
    {
        var value_3: i64[>..] = [input + 4];
        print(value_3[0]);
    }
}

recursive fn second(input: i64) {
    var value: i64[>..] = [input];
    print(value[0]);
    {
        var value: i64[>..] = [input + 1];
        print(value[0]);
    }
}

fn main() {
    let first_input = value(marker(value) as i32);
    let second_arg: i16 = 20;
    let second_input = value(second_arg);
    first(first_input);
    second(second_input);
    first(30);
}
