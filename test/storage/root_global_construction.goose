// runtime-debug: a component can grow while another is under construction.
root struct State { first: u8[][>..], second: u8[][>..] }
var state: State;
fn extra() -> u8[] { state.second.push("a"); "b" }
fn main() {
    state.first.push(extra());
    state.first.push(extra());
    var out: u8[>..] = [];
    for text in state.first { out.append(text); }
    out.push(' ');
    for text in state.second { out.append(text); }
    print(out);
}
