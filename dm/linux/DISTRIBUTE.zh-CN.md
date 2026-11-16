# 公共软件包与私人准备结果

`package_runtime.py` 和 `package_preparer.py` 通过显式文件清单收集软件，不遍历玩家安装目录。

## 公共软件

```sh
python3 dm/linux/package_runtime.py --build-dir /自己的/Release输出   --out /自己的/new-runtime.zip --with-preparer
```

目标 ZIP 必须尚不存在。运行时须通过 ARM64 ELF、Release、kind、游戏版本和当前源码身份检查；诊断程序、短 ELF 假头、旧源码产物不能发布。包中保留项目与第三方许可。

`--with-preparer` 添加固定闭集的电脑准备源码。`--wayland-wsi /显式审核的WSI目录` 只接收约定的库、相对路径 manifest、来源和许可；WSI 的驱动适用性另行验证，见 [WSI.md](WSI.md)。

## 私人生成物

`device_release.py` 将公共软件和私人 preparation 输出分开。私人目录可包含玩家自己准备的游戏；不能将其整体当作公开软件包上传。所有游戏包、Key、改写模块、生成着色器、存档及缓存都排除在公共源码和 runtime 包之外。

制作使用说明时沿用[三步指南](PACKAGED-INSTALL.zh-CN.md)，只写当前命令与限制。实验日志、截图、设备路径和逐轮性能记录不随包分发。
