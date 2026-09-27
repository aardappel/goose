// A widget the program should not have made: outside any window. The ui
// layer reports it as it happens and skips it, and the next frame's
// input_begin aborts the program with the reason.
import std;
import ui;

fn main() {
    let ctx = ui::create(13.0);
    ui::input_begin(ctx);
    ui::input_end(ctx);
    print("a button outside any window");
    print("skipped, not pressed: ", !ui::button_label(ctx, "Lost"));
    ui::input_begin(ctx);
    print("not reached");
}
