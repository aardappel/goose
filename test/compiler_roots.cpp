// Laws of the provenance domain, independent of the order branches/calls
// discover roots. Keep assertions active even in an optimized test build.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/includes.h"
#include "../src/utils.h"
#include "../src/lexer.h"
#include "../src/ast.h"

using namespace goose;

static Roots One(const RootAlt &a) {
    Roots r;
    r.Add(a);
    return r;
}

static Roots Join(Roots a, const Roots &b) {
    a.Add(b);
    return a;
}

int main() {
    VarDef root, other, source, other_source;
    vector<RootAlt> alternatives;
    for (auto from : { (VarDef *)nullptr, &source, &other_source })
        for (int bits = 0; bits < 8; bits++)
            alternatives.push_back({ &root, bool(bits & 1), from,
                                     bool(bits & 2), bool(bits & 4) });
    for (auto &a : alternatives) {
        auto x = One(a);
        assert(!x.Add(a));                         // Idempotence.
        for (auto &b : alternatives) {
            auto y = One(b);
            auto joined = x;
            auto changed = joined.Add(b);
            assert(changed == (joined != x));     // Feedback notices every weakening.
            assert(joined == Join(y, x));         // Commutativity.
            for (auto &c : alternatives) {
                auto z = One(c);
                assert(Join(Join(x, y), z) == Join(x, Join(y, z)));
            }
        }
    }
    auto x = One({ &root, true, &source, true, true });
    auto y = One({ &other, false });
    y.Add({ nullptr, true });                      // Static data is a root too.
    assert(Join(x, y) == Join(y, x));
    assert(Join(x, y).Same(Join(y, x), Roots::Compare::Cycle));
    assert(Join(x, y).Same(Join(y, x), Roots::Compare::GlobalReach));

    auto changed_source = One({ &root, true, &other_source, true, true });
    assert(x != changed_source);
    assert(x.Same(changed_source, Roots::Compare::Cycle));
    assert(!x.Same(changed_source, Roots::Compare::GlobalReach));
    auto changed_reads = One({ &root, true, &source, false, false });
    assert(x != changed_reads);
    assert(!x.Same(changed_reads, Roots::Compare::Cycle));
    assert(x.Same(changed_reads, Roots::Compare::GlobalReach));
    auto unknown = x;
    unknown.unknown = true;
    assert(x != unknown);
    assert(!x.Same(unknown, Roots::Compare::Cycle));
    assert(x.Same(unknown, Roots::Compare::GlobalReach));

    Roots pending, empty;
    pending.SetUnknown();
    assert(empty.Add(pending));                    // A change in discovery state is feedback too.
    assert(empty == pending);
    assert(!empty.Add(pending));
    auto with_pending = x;
    assert(with_pending.Add(pending));
    assert(with_pending == unknown);
    assert(!with_pending.Add(pending));

    Prov p, q;
    p.TakeAlts(Join(x, y));
    q.TakeAlts(Join(y, x));
    assert(p == q);
    q.writable = true;
    assert(p != q);
    q = p;
    q.freshview = true;
    assert(p != q);
}
