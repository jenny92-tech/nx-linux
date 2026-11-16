# ARM64 Linux runtime

This directory contains the Linux service, desktop preparation tools, installer and synthetic regression suite. Vulkan is the maintained graphics path; OpenGL ES is experimental.

## Entry points

- [Prepare and install](PREPARE.zh-CN.md)
- [Build and validate](BUILD.md)
- [Runtime architecture and policy](DEVELOPMENT.md)
- [Public software packaging](DISTRIBUTE.zh-CN.md)
- [WSI ownership and requirements](WSI.md)
- [Device profiles](devices/README.md)

The default `rk3566-2gb` adaptation uses compact classic resources and a flat menu. Its name is a target, not proof of gameplay on every 2 GiB device. Current gameplay evidence comes from MiniLoong RK3566/4 GiB. Other firmware/driver combinations, including Rocknix, require separate acceptance; 1 GiB support is not established.

Game builds 90447 and 94012 require their own matching modules and runtime. Existing saves/settings remain in the selected player profile. A 94012 resource bundle must not be mixed into a 90447 installation.

At the 94012 main character menu, B opens the exit confirmation, A confirms and B cancels. Return there with the game's existing save-and-exit operation; pending save transactions must finish before exit.

See [AGENTS.md](AGENTS.md) for the source map and local maintenance contracts. Per-run measurements, hardware captures and experimental promotion notes are private working material, not public source documentation.
