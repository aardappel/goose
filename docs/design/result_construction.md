# Construction through function results

Fresh values containing relative references can be returned by named
functions and trailing blocks, selected by control flow, and forwarded
through other calls. A reference to an existing element must be encoded at
the receiving slot, whose root must also contain that element.

This is general construction behavior. It requires no pool type, allocator
builtin, field-privacy feature, or annotation on a builder function.

## Checker model

Three facts remain separate:

* The result's type, inferred or declared by its producer. Trailing blocks
  still infer their own type; a bare `[]` gets no contextual element type.
* Whether a value is fresh construction or a copy of existing storage.
  Block tails and each selected value path obey the existing copy rules.
* The receiving storage. Arguments, operand temporaries and discarded
  expressions have their own locations and never borrow an enclosing
  result's destination.

A named return has a symbolic receiver. Relative initializers add
requirements relating that receiver to the roots that contain their targets.
Calls map those roots through their arguments and check the requirements
against the actual receiver. A forwarding return carries the requirement
on to its own caller. Captured roots in trailing blocks use the same
mechanism when the receiving array is a parameter of a higher-order
function.

The checker records these requirements in the function's summary and validates
them at every call, including cached specializations, dispatch cases, and
recursive rounds. It does not infer a guarantee from the first caller alone. A
local root that ends with the returning activation cannot satisfy a result
requirement. Nonlocal returns map requirements through each intervening call to
their actual target.

Existing proofs within a parameter class remain local to that activation:
an argument whose precise outer root is unknown can still link two
elements reached through the same parameter.

## Emission

Fixed aggregate results containing relative references use typed output
pointers, regardless of size. A receiving stack slot supplies its final
address. Normal returns, multiple-result forwarding, case dispatch,
inlined blocks and nonlocal returns use that address directly. The ABI for
ordinary fixed results is unchanged, and no optimizer is needed for
correct placement.

Representation conversions preserve self-contained `self` and null links when
they preserve the links' region. If a result depends on its receiver's root, an
adapter cannot stage or relocate its bytes. The result can use an identical
representation or construct a variant directly behind its ADT tag. It cannot be
used as a resize fill, because that operation constructs a value once and copies
it to multiple slots.

The existing growth, shrink, lifetime and partial-construction rules remain
in force. A callback executes once at the construction site. A push exposes
its new length only after the element is complete.

## Validation and integration

Regression fixtures cover fixed and variable aggregates, `map`, named and
inferred returns, forwarded callbacks, valued breaks, multiple returns,
case dispatch, recursion, pool-relative `self`, and nonlocal returns.
Rejection fixtures cover existing-storage copies, wrong roots on later
calls, cached callbacks, local and operand temporaries, argument isolation,
discarded results, conflicting growth, resize fills and layout conversion.
Native C and TinyCC runs exercise both `-O0` and `-O2`, with runtime checks.

These compiler changes landed independently on master before the pool
branch was rebased. `slot_pool.alloc_with` uses ordinary library control
flow: assign a builder result into a recycled slot, or push it into a new
one. It needs no additional compiler privilege.
