# Divergence from upstream OpenAttributeGraph

This fork exists so Darling can ship a Swift-ABI-compatible `SwiftUI.framework` built from
OpenSwiftUI, which depends on this package. Branch `darling`, forked from upstream
`1d8e762338d6447c0b8c98cd08ccf1efca981e82`.

Changes are sorted into two categories. **General fixes** are real defects that reproduce
outside Darling and should be offered upstream. **Darling adaptations** encode assumptions that
are only true for Darling's SDK and must never be sent upstream as-is.

## General fixes (offer upstream)

| Commit | File | What |
|---|---|---|
| `23d0ca0` | `Sources/OpenAttributeGraphShims/DebugClient.swift` | `import Network` was gated on `canImport(Darwin)` rather than on `canImport(Network)`. Any Darwin target whose SDK lacks Network.framework fails to build the whole module. |

## Darling adaptations (keep in the fork)

| Commit | File | What |
|---|---|---|
| (this) | `Sources/OpenAttributeGraph/Graph/Graph.swift` | `Graph.archiveJSON(name:)` is compiled out under `-DOPENATTRIBUTEGRAPH_DARLING`. It is a debug helper that dumps the graph to a JSON file; it needs `JSONSerialization` and `Dictionary`-to-`NSDictionary` bridging, neither of which Darling's deliberately partial Foundation overlay provides. Nothing else in the package calls it. |

## What is NOT changed, and why that matters

The blocker previously reported against this package was "128 C++-interop errors under Swift
6.3.3". Those errors were an artifact of the probe's own invocation, not a defect here.

`OAG_OPTIONS` is `CF_OPTIONS` (`OAGBase.h`), and `CF_OPTIONS` in the SDK's `CFAvailability.h`
branches on `__cplusplus`: the C++ expansion is `typedef uint32_t X;` plus an *anonymous* enum,
so every option set imports into Swift as a bare integer instead of an `OptionSet` struct. That
one degradation accounted for roughly 60 of the 64 distinct errors (`OAGValueOptions (aka
'UInt32')`, `has no member 'contains'`, `'Flags' is not a member type of ...`).

The probe had passed `-cxx-interoperability-mode=default`. Upstream's own `Package.swift` never
builds this target that way: `openAttributeGraphTarget` uses plain `sharedSwiftSettings`, and
only the test targets call `.addCxxInterop()`. The Swift target consumes the **C** submodule of
`Sources/OpenAttributeGraphCxx/include/module.modulemap`.

**Do not add C++ interop to the `OpenAttributeGraph` target.** Compile it with the plain Clang
importer and the option sets import correctly.

## Build settings Darling uses

    -target arm64-apple-macosx26.0
    -swift-version 5 -parse-as-library -enable-library-evolution
    -enable-upcoming-feature InternalImportsByDefault
    -enable-experimental-feature Extern
    -D OPENATTRIBUTEGRAPH -D OPENATTRIBUTEGRAPH_DARLING
    -Xcc -DOPENATTRIBUTEGRAPH
    -Xcc -I Sources/OpenAttributeGraphCxx/include
    -Xcc -I Sources/Utilities/include
    -Xcc -I Sources/Platform/include
    -I <darling-swift overlays>/out-foundation-min/modules      # Foundation overlay, NOT FoundationEssentials
    -I <darling-swift overlays>/out/modules
    -Xcc -fmodule-map-file=<darling-swift overlays>/Foundation/shims/module.modulemap

Both `OpenAttributeGraph` and `OpenAttributeGraphShims` build with zero errors this way.

## Licence

Upstream is MIT (see `LICENSE`), retained unchanged.
