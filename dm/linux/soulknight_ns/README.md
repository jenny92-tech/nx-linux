# Soul Knight NS

Switch 1.2.0、Unity 2018.4.12f1 IL2CPP 的 ARM64 Linux 原生适配，题号 `0100a0800b83a000`。
仅接受匹配模块身份；原 SDK、模块、资源、字体与生成着色器由玩家显式准备，不进入公开源码。

共享 NS/NVN 实现在 `dm/host/`、`dm/gfx/nvn/`；本目录提供标题服务、原 Unity 尺寸重建与独立构建身份。
玩家启动、设置、资源装配和档案保持归
[Launcher Pack](https://github.com/jenny92-tech/portmaster-launcher-pack/tree/main/ports/soulknight_ns)。

## 构建

`build.py` 只复用已存在且身份匹配的交叉构建镜像与 Vulkan 头文件，不获取工具链或驱动。
先准备构建脚本声明的环境，再从仓库根运行：

```sh
python3 dm/linux/soulknight_ns/build.py
python3 dm/linux/soulknight_ns/build.py --backend gles
```

输出分别为 `dm/linux/out/soulknight_ns/` 和 `dm/linux/out/soulknight_ns_gles/`，
含 stripped ARM64 ELF 与真实源码收据。GLES 使用独立实验身份，不能通过 Vulkan 正式产物门禁。
两种后端需要各自完整的离线着色器 bank；可构建不等于全部阶段已编译或整游验收。

## 不变量与验证范围

- `resolution_policy.h` / `aspect_policy.h` 只额外接纳所选精确内部尺寸，保留其他请求的原标志与判定；通过原 Unity 接口重建相机/UI。
- `module_mapping.h` 的私有文件映射保留重定位、RX 封页及尾页补零；`alias_backing.h` 保护别名、活邻页、空间准入及撤回后再曝光清零。
- Vulkan 常量快照/可见性前缀与 GLES 常量版本/有条件同步过滤均保留真实写者、观察者、GPU 完成和最终回写。
- 默认关闭的纹理缓存、热池和延后回收候选不因源码存在而自动启用；预算与文件后备并不等于实际节省 RAM。

已有 MiniLoong G52 Vulkan 和 TrimUI G57 Vulkan/GLES 的有限菜单、地牢及退出观察；
不同 API、驱动、场景和测量构建分别记账，不承诺全关卡、稳定 60 FPS 或任意 1 GiB 设备可玩。
GPU 字节/像素、文件映射与 A64 夹具在 `dm/linux/tests/` 和本目录 `tests/`，不得用合成检查替代设备验收。

2026-11-12—15 的历史固定构建有下列有限对照；本次公开源码的交叉编译没有新增设备测量：

| 设备与场景 | 呈现速率观察 | 条件与限制 |
|---|---|---|
| MiniLoong RK3566 / G52 g13p0、Vulkan/Wayland、角色选择大厅 | 约 17.5→57.3/s | 同一 ELF 开/关/关/开，双方启用 snapshot，仅改变 shader-barrier flag；计时/抓图诊断相同，不是战斗或物理刷新率 |
| TrimUI A523 / G57 r32p0、GLES/KMSDRM、角色选择大厅 | 约 8.28→41.13/s | 同内部尺寸，常量版本/合并与有条件同步过滤；未跳过绘制 |
| 同一 TrimUI GLES 的有限真实战斗 | 窗口中位数约 55.87/s、最低 24.08/s | 含抓图且不是固定回放；不据此算战斗提帧比例或承诺稳定 60 |

以上没有建立整体内存减少结论。MiniLoong 的较早 GLES 试验仍约 5–8/s，未完成战斗验收；
不同 GPU、驱动、呈现路径与场景不能直接互套。
