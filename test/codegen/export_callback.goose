// An export called by C that Goose code called, before that C returns, is a
// runtime error (§7.11): the export would run over the stacks of the Goose
// code below it, whose tops that code may still hold in locals. The
// sidecar's header gives call_back, C that calls the export.
extern fn call_back(n: i64) -> i64;

export "on_callback" fn on_callback(n: i64) -> i64 {
    let s = str("callback ", n);
    return s.len;
}

fn main() {
    let local = str("main local ", 42);
    print(local, " ", on_callback(1));    // a Goose call to an export is a plain call
    print(local, " ", call_back(2));
}
