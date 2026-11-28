enum Mode { Off, Level { value: i64 } }
enum Message { Empty, Text { value: u8[] } }
enum Box<T> { Empty, Value { value: T } }
enum Choice { Empty, Data { value: i64, text: u8[] } }

fn level(value: i64) -> Mode.Level { Mode.Level { value: value } }
fn read_enum() -> Mode { print("enum"); Mode.Level { value: 7 } }
fn read_variant() -> Mode.Level { print("variant"); Mode.Level { value: 7 } }

fn get_ref<T>(value: T&) -> T& { value }

fn set_value(choice: Choice..&, value: i64) {
    match choice { Data &data => { data.value = value; }, Empty => {} }
}

fn change(choice: Choice..&) -> Choice.Data {
    set_value(choice, 2);
    return Choice.Data { value: 2, text: "text" };
}

fn change_enum(choice: Choice..&) -> Choice.. { change(choice) }

fn main() {
    let fixed: Mode = Mode.Level { value: 7 };
    let variant = Mode.Level { value: 7 };
    print(fixed == variant, " ", variant == fixed);
    print(fixed != level(8), " ", level(8) != fixed);
    print(fixed == Mode.Off {}, " ", Mode.Off {} != fixed);

    let empty: Mode = Mode.Off;
    print(empty == Mode.Off {}, " ", Mode.Off {} == empty);

    let message: Message.. = Message.Text { value: "hello" };
    let text = Message.Text { value: "hello" };
    print(message == text, " ", text != message);
    print(Message.Text { value: "other" } == message, " ", message != Message.Empty {});

    let boxed: Box<i64> = Box<i64>.Value { value: 8 };
    print(Box<i64>.Value { value: 8 } == boxed);
    print(read_enum() == read_variant());
    print(read_variant() == read_enum());

    var choice: Choice.. = Choice.Data { value: 1, text: "text" };
    var whole: Choice.. = Choice.Data { value: 1, text: "text" };
    assert((choice == change(choice)) == (whole == change_enum(whole)));
    set_value(choice, 1);
    set_value(whole, 1);
    assert((get_ref(choice) == change(choice)) == (get_ref(whole) == change_enum(whole)));
    set_value(choice, 1);
    set_value(whole, 1);
    assert((choice != change(choice)) == (whole != change_enum(whole)));
    set_value(choice, 1);
    set_value(whole, 1);
    assert((get_ref(choice) != change(choice)) == (get_ref(whole) != change_enum(whole)));
}
