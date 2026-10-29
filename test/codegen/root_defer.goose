// runtime-debug all-levels
import slot_pool;
import slice_pool;

root struct State { first: i64[>..] = [], second: i64[>..] = [] }
struct Holder { state: State& }
var exits: i64[>..] = [];

fn finish(state: State&, held: Holder&) {
    state.first.push(2);
    held.state.first.push(3);
    state.first.push(4);
    held.state.second.push(5);
    assert(state.first == [1, 2, 3, 4]);
    assert(state.second == [5]);
    exits.push(state.first.len + state.second.len);
}

fn early() -> i64 {
    var state = State { .. };
    var held = Holder { state };
    defer finish(state, held);
    state.first.push(1);
    return state.first.len;
}

fn local_pools() {
    for i in 4 {
        var slots = slot_pool<i64> {};
        var runs = slice_pool<i64> {};
        let node: i64& = slots.alloc_with() { i };
        let values = runs.alloc_slice(3);
        values[0] = i;
        defer {
            assert(node == i && values[0] == i);
            slots.free_ref(node);
            runs.free_slice(values);
            let reused = slots.alloc_ref(i + 10);
            assert(slots.items.len == 1 && reused == i + 10);
            let again = runs.alloc_slice(3);
            assert(runs.items.len == 3 && again == [0, 0, 0]);
            exits.push(reused);
        }
        if i == 0 { continue; }
        if i == 2 { break; }
    }
}

recursive fn unwind(state: State&, depth: i64) {
    defer {
        var local = State { .. };
        local.first.push(depth);
        local.first.push(depth + 10);
        assert(local.first == [depth, depth + 10]);
        state.first.push(local.first[0]);
        state.second.push(local.first[1]);
    }
    if depth > 0 { unwind(state, depth - 1); }
}

fn leave() {
    var state = State { .. };
    var held = Holder { state };
    defer finish(state, held);
    state.first.push(1);
    return 7 from target;
}

fn target() -> i64 {
    var state = State { .. };
    defer { state.second.push(20); exits.push(state.second[0]); }
    leave();
    return 0;
}

fn main() {
    assert(early() == 1);
    local_pools();
    var one = State { .. };
    var two = State { .. };
    unwind(one, 2);
    unwind(two, 1);
    assert(one.first == [0, 1, 2] && one.second == [10, 11, 12]);
    assert(two.first == [0, 1] && two.second == [10, 11]);
    assert(target() == 7);
    assert(exits == [5, 10, 11, 12, 5, 20]);
    print("root defer: ok");
}
