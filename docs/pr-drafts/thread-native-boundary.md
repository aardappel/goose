I've been looking at how Goose's worker isolation interacts with native adapters, and wanted to clarify one small boundary in the tutorial.

The copied globals and flat queue values already give ordinary Goose code its isolation. But a copied integer can still name the same native resource, and an extern can reach shared state outside that model. This qualifies section 14's race-freedom statements and adds a short limitation in section 18, without changing any behavior or introducing new APIs.

I kept the existing host-thread restriction in section 19 as it is. Checked the wording against the worker/global lowering and native call boundary, and checked the section link and diff whitespace.
