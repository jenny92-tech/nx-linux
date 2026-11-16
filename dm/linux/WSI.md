# Wayland WSI integration

The optional `VK_LAYER_RUDROID_wayland` component is maintained by Rudroid. D2R consumes the library and layer manifest; it does not implement a second WSI layer.

## Implementation boundary

The examined C197 library has SHA256 `2b0084f9cf6f17d142d5c75b4900e82a30cafb1b7cfa75515024cb974025c58c`. Its WSI implementation is C++; Rust supplies a separate GBM allocator.

The loader negotiation export installs the layer's instance/device procedure callbacks. The instance callback resolves the internal Wayland `CreateWaylandSurfaceKHR` implementation when the relevant extensions are enabled. Hidden visibility means this function need not appear as a global dynamic export.

The wrapper also calls the next dispatch table's `CreateWaylandSurfaceKHR`. The layer therefore needs a compatible loader/driver chain and a real Wayland display. Its internal function being present does not establish that every Rocknix driver combination can create a surface.

## Source and diagnosis

The C197 source supplement contains the receipt-matched patched C++ source, fixed headers/protocols, original Rust/build sources, licenses and provenance notes. Historical allocator-source correspondence and generated-file limits are stated in that supplement; it is not presented as a verified standalone rebuild.

For a compatibility report, provide the exact layer-library SHA256, device/OS version, Vulkan driver/loader identity, layer manifest and the minimal original error text. Do not upload a full environment dump, player files or unfiltered diagnostics. An export-list observation alone does not identify the failing link in the chain.

Rudroid source: <https://github.com/jenny92-tech/Rudroid>. Layer setup is local to the game process and must preserve the system driver.
