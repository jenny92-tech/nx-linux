# Ryujinx shader translator (vendored subset)

Three projects from Ryujinx 1.3.2: `Ryujinx.Graphics.Shader`, `Ryujinx.Common` and `Spv.Generator`.
The shader translation tool in `dm/gfx/tools/dmsh_xlate` references them to translate the original Switch shaders to SPIR-V.
License: MIT, see `LICENSE.txt`. Upstream: <https://github.com/Ryubing>.

Ryujinx 1.3.2 的三个项目：`Ryujinx.Graphics.Shader`、`Ryujinx.Common`、`Spv.Generator`。
`dm/gfx/tools/dmsh_xlate` 的着色器翻译工具引用它们，把 Switch 原版着色器翻译成 SPIR-V。许可证 MIT，见 `LICENSE.txt`。

Local fix: `StructuredIr/AstOptimizer.cs` keeps locals passed by pointer to
function calls. Expression propagation previously replaced input-argument
temporaries with expressions or removed output-argument locals. SPIR-V call
generation needs their original addresses. The fix applies to both call uses
and definitions; other expression propagation remains enabled.

本地修复：`StructuredIr/AstOptimizer.cs` 保留传给函数调用的地址变量，
包括输入临时变量和输出参数；其他表达式传播仍保持启用。
