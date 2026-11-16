# NX Linux

在 ARM64 Linux 上运行玩家自备的 Switch 版《暗黑破坏神 II：狱火重生》，并在电脑上准备配套资源。框架适配原程序的系统服务和 NVN 图形调用，不是通用 Switch 模拟器。

English: [README.md](README.md)。

元气骑士 NS 1.2.0 的独立标题服务、双后端构建和范围见 [标题说明](dm/linux/soulknight_ns/README.md)。

## 当前范围

- Linux Vulkan 后端，支持 90447、94012 对应的经典资源准备。
- 默认 `rk3566-2gb` 使用平面经典菜单。2 GB 是适配目标，不代表所有 2 GB 设备已通过游玩验证。
- 已记录的游戏验证来自 MiniLoong RK3566 / 4 GiB；Rocknix、其他驱动及固件需要分别验证。尚未建立 1 GiB 可玩支持。
- OpenGL ES 仍属实验路径。ARM64 ABI 匹配不能替代画面、输入、音频和存档验证。

## 准备与安装

1. 将自己持有的匹配本体、更新包和 Key 放入私人输入目录。
2. 运行 `bash dm/linux/prepare-classic.sh --device rk3566-2gb --input /自己的/input --out /自己的/prepared`。默认制作中文/英文经典精简资源；确需附加资源时才用 `--resources full`。
3. 将准备结果和匹配的 Release 运行时安装到掌机，沿用原有角色及设置。详见[准备说明](dm/linux/PREPARE.zh-CN.md)与[设备路径](dm/linux/devices/README.md)。

仓库不包含游戏、Key、改写后的游戏模块、生成着色器、存档或驱动缓存；这些内容由玩家在自己的电脑准备。

## 开发

[构建](dm/linux/BUILD.md) · [架构与运行策略](dm/linux/DEVELOPMENT.md) · [WSI 接入](dm/linux/WSI.md) · [贡献约定](CONTRIBUTING.zh-CN.md) · [安全说明](SECURITY.md)

共享 host、graphics 和翻译器源码沿用上游布局。移动端前端源码为保持来源连续性而保留，本分支的发行与验证范围为 ARM64 Linux。

## 来源与许可

源自 d2r-mobile，来源和第三方条款见 [SOURCE-ORIGINS.md](SOURCE-ORIGINS.md)。保留原有 [MIT 许可](LICENSE)及上游声明；可选 Rudroid WSI 另有源码和许可要求。本项目不授予原游戏内容的分发权。
