// A generic type's fields may name it again, directly or through other
// declarations, as long as that reaches finitely many instantiations
// (§3.2): with the same arguments, others that name none of its
// parameters, the same ones in another order, or ones with only a
// reference or const around them. A type nested in its own arguments
// chains one instantiation per level.

// The same argument: L<i64> holds an L<i64>.
struct L<T> { v: T, next: L<T>? }

// An argument naming no parameter: R<u8> holds an R<u8[]>, which holds
// another R<u8[]>.
struct R<T> { v: T, alt: R<u8[]>? }

// The arguments swapped: F<i64, bool> holds an F<bool, i64>, which holds
// an F<i64, bool>.
struct F<A, B> { a: A, b: B, flip: F<B, A>? }

// A reference around the argument: W<i64> holds a W<i64&>, which holds
// another, a reference to a reference being that reference (§3.8).
struct W<T> { v: T, r: W<T&>? }

// const around the argument: C<i64> holds a C<const i64>, which holds
// another.
struct C<T> { v: T, r: C<const T>? }

// Two declarations naming each other with the same argument.
struct A<T> { v: T, b: B<T>? }
struct B<T> { w: T, a: A<T>? }

// A larger argument once, into a type that names itself with the same one:
// Tagged<i64> holds an L<i64[]>, which holds an L<i64[]>.
enum Tagged<T> { Plain { v: T }, Linked { l: L<T[]>? } }

// Sixteen instantiations of Wrap, each needed by the one before, are the
// longest chain one declaration may have; more made one after another are
// fine.
struct Wrap<T> { inner: T }
type Deep = Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<Wrap<i64>>>>>>>>>>>>>>>>;

fn held(l: L<i64[]>?) -> i64 {
    if l != null { return l.v.len; }
    return -1;
}

fn main() {
    let l1 = L<i64> { v: 1, next: null };
    let l2 = L<i64> { v: 2, next: l1 };
    let ln = l2.next;
    if ln != null { print(l2.v + ln.v); }

    let r1 = R<u8[]> { v: [4, 5], alt: null };
    let r2 = R<u8> { v: 6, alt: r1 };
    let ra = r2.alt;
    if ra != null { print(ra.v.len); }

    let f1 = F<bool, i64> { a: true, b: 7, flip: null };
    let f2 = F<i64, bool> { a: 8, b: false, flip: f1 };
    let ff = f2.flip;
    if ff != null { print(f2.a + ff.b); }

    var x = 9;
    let w1 = W<i64&> { v: x, r: null };
    let w2 = W<i64> { v: 10, r: w1 };
    let wr = w2.r;
    if wr != null { print(w2.v + wr.v); }

    let c1 = C<const i64> { v: 11, r: null };
    let c2 = C<i64> { v: 12, r: c1 };
    let cr = c2.r;
    if cr != null { print(c2.v + cr.v); }

    let a1 = A<i64> { v: 13, b: null };
    let b1 = B<i64> { w: 14, a: a1 };
    let ba = b1.a;
    if ba != null { print(b1.w + ba.v); }

    let l3 = L<i64[]> { v: [15, 16, 17], next: null };
    let t: Tagged<i64> = Tagged<i64>.Linked { l: l3 };
    let n = match t { Linked k => held(k.l), Plain p => p.v };
    print(n);

    var deep = default<Deep>();
    deep.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner = 16;
    print(deep.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner.inner);
    let flag = Wrap<bool> { inner: true };
    print(flag.inner);
}
