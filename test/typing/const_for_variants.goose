// const for over an ADT's variants (const_for.md §10): variants<E>, fields<V>
// over a variant's payload, and match arm lists.
enum Kind { Town, Catacombs, Burrows }
enum Shape { Circle { r: i64 }, Rect { w: i64, h: i64 }, Dot }

fn number(k: Kind) -> i64 { match k { Town => 10, Catacombs => 11, Burrows => 12 } }

// A number back to its variant, for every payload-less enum with a number().
fn from_number<E>(n: i64, fallback: E) -> E, bool {
    const for <N, V> in variants<E> {
        if number(V) == n { return V, true; }
    }
    return fallback, false;
}

fn numbers_distinct<E>() -> bool {
    const for <N1, V1> in variants<E> {
        const for <N2, V2> in variants<E> {
            if N1 != N2 && number(V1) == number(V2) { return false; }
        }
    }
    true
}

// Every variant with its payload's fields, by value (fixed mode) ...
fn show<T>(value: T) {
    match value {
        const for <N, V> in variants<T> {
            V v => {
                print(N);
                const for <F, A> in fields<V> { print("  ", F, " = ", v.A); }
            },
        }
    }
}

// ... or by reference (variable mode).
fn show_ref<T>(value: T&) {
    match value {
        const for <N, V> in variants<T> {
            V &v => {
                print(N, " (by reference)");
                const for <F, A> in fields<V> { print("  ", F, " = ", v.A); }
            },
        }
    }
}

// Each variant's payload handed on, as a case function's call would.
fn each_case<T, F>(value: T) {
    match value { const for <N, V> in variants<T> { V c => F(N, c), } }
}
fn area(c: Shape.Circle) -> i64 { c.r * c.r * 3 }
fn area(r: Shape.Rect) -> i64 { r.w * r.h }
fn area(d: Shape.Dot) -> i64 { 0 }

// Every variant built with each payload field set: a variant constructor's
// initializers expanded over fields<V>.
fn filled<E>(fill: i64) {
    const for <N, V> in variants<E> {
        let e: E = V { const for <F, A> in fields<V> { A: fill, } };
        show(e);
    }
}

// Generated arms beside a final _ (an ADT), and a constant list's arms beside
// written ones and the required _ (an integer).
fn has_payload(s: Shape) -> bool {
    match s { const for <N, V> in variants<Shape> { V v => N != "Dot", }, _ => false }
}
fn size_name(n: i64) -> u8[] {
    match n { 0 => "none", const for <C> in [1, 2, 4] { C => "power", }, 3 => "three", _ => "other" }
}

fn main() {
    let k, ok = from_number(11, Kind.Town);
    print(number(k), " ", ok);
    let j, no = from_number(5, Kind.Burrows);
    print(number(j), " ", no);
    print(numbers_distinct<Kind>());
    let a: Shape = Shape.Circle { r: 2 };
    let b: Shape = Shape.Rect { w: 3, h: 4 };
    let c: Shape = Shape.Dot;
    show(a); show(b); show(c);
    var d: Shape.. = Shape.Rect { w: 5, h: 6 };
    show_ref(d);
    each_case(b) { name, payload => print(name, " area ", area(payload)) }
    filled<Shape>(7);
    print(has_payload(a), " ", has_payload(c));
    print(size_name(0), " ", size_name(2), " ", size_name(3), " ", size_name(4), " ", size_name(9));
}
