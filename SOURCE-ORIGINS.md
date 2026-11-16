# Source origins

Public repository: [jenny92-tech/nx-linux](https://github.com/jenny92-tech/nx-linux). This publication adds the separate Soul Knight NS adapter while retaining upstream source attribution and title-specific validation boundaries.

This Linux fork is based on d2r-mobile. The local source baseline is commit `3cfb9e806e1ba45428e816b84112cf099f900f8b`, obtained from `dodola/d2r-mobile`; the original upstream is `jhaizhou-ops/d2r-mobile`. Linux service, resource preparation and shared graphics changes are maintained in this fork.

The existing MIT license and code attribution are preserved. Third-party source files retain their own terms; the root license does not replace them.

| Component | Local source or notice |
|---|---|
| Original project | `LICENSE`, original source headers |
| Ryujinx shader translation | `dm/third_party/ryujinx/README.md`, `dm/third_party/ryujinx/LICENSE.txt` |
| SPIR-V generator | `dm/third_party/ryujinx/src/Spv.Generator/LICENSE` |
| BC texture decoder | `dm/third_party/bcdec/LICENSE` |
| NVN interface declarations | `dm/linux/licenses/NVN-interface-MIT.txt` |
| deko3d-related interface code | `dm/linux/licenses/deko3d-zlib.txt` |
| Optional Rudroid WSI | Supplied separately with its source receipt, original notices and license texts |

Interface facts needed for compatibility are described in [DERIVED](docs/DERIVED.md). The original game executable, art, shaders, assets, saves and keys are not project source and are not distributed here.

The first public Linux commit is a reviewed source snapshot. It does not import the incomplete upstream history from the local shallow checkout.
