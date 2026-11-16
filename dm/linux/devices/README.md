# 机型配置与路径

默认 `rk3566-2gb` 为经典平面菜单，保留中文/英文经典资源。2 GB 是适配目标，不能依据目录名宣称设备已验证；现有完整游戏验证来自 MiniLoong RK3566 / 4 GiB。

配置中的实际硬件资料和能力验证分别维护。另保留的 4 GiB profile 用于既有资源兼容/开发，不作为另一份默认 NS 发行包推广。1 GiB 和其他固件尚需独立验收。

```sh
bash dm/linux/prepare-classic.sh --device rk3566-2gb   --input /自己的/input --out /自己的/prepared
```

准备结果在私人 `ready/` 内。安装使用显式 `--runtime-root`、`--profile-root`、`--controllers` 和 `--entry`，或由匹配 profile 选择默认值。原有角色与设置不覆盖，profile 放在支持所需文件语义的可写文件系统。

可见脚本使用 `Je-暗黑破坏神II_NS（2G版）.sh`。内部目录和 profile 路径不随可见名字改动。不同版本的 guest 模块、资源和 runtime 必须成套验证。

公开软件与私人游戏结果的生成关系见 [DISTRIBUTE](../DISTRIBUTE.zh-CN.md)。硬件验收至少覆盖显示、输入、音频、场景往返、保存退出和重新加载；源码/打包检查只证明各自的静态契约。
