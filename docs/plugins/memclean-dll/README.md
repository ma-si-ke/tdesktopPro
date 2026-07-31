# memclean 插件源码

内存清理插件的 DLL 源码。核心技术来自 [Mem Reduct](https://github.com/henrypp/memreduct)
（(c) Henry++，GPL-3.0），本实现为自包含移植：不依赖 routine 库，只依赖 ntdll / kernel32 / advapi32。

**License: GPL-3.0-or-later**（继承自 Mem Reduct 的实现思路与掩码设计）。

## 文件

| 文件 | 用途 |
|---|---|
| `memclean.c` / `memclean.h` | 清理本身（NT API 调用），与插件接口无关 |
| `plugin.c` | 插件 ABI 层：`Plugin_Abi` / `Plugin_Init` / `Plugin_Call` / `Plugin_Shutdown` |
| `plugin.json` | 清单，随 DLL 一起打包 |
| `CMakeLists.txt` | 构建脚本 |

插件接口的完整说明见 [`../README.md`](../README.md)。

## 构建

```bat
cmake -B build && cmake --build build --config Release
```

或 MSVC 一行：

```bat
cl /nologo /W4 /O2 /utf-8 /DUNICODE /D_UNICODE /DMEMCLEAN_EXPORTS /LD memclean.c plugin.c /link /OUT:memclean.dll
```

产物是 x64 DLL。要出 32 位或 ARM64 包，源码无架构相关代码，直接换目标架构重编即可。

## 打包发布

zip 根目录直接放两个文件，不要多套一层目录：

```
memclean-v2.zip
  ├── plugin.json
  └── memclean.dll
```

上传后在 `https://td.kakaco.top/api/plugins` 加一条，`version` 必须与 `plugin.json` 里的一致。

## 行为

- **提权**：清理用的 NT API 需要 `SeProfileSingleProcessPrivilege` 和
  `SeIncreaseQuotaPrivilege`，只有管理员进程能启用。`Plugin_Init` 检测到未提权时返回
  `available: false`，主程序会把界面整体置灰并显示原因。
- **清理范围**：固定用 Mem Reduct 的默认掩码（`MC_MASK_DEFAULT`），
  **不含** `MC_STANDBYLIST` 和 `MC_MODIFIEDLIST`——清空整个待命列表等于丢弃全部磁盘缓存，
  清完系统会明显卡顿。别把这两项加回去。
- **自动清理**：`tick` 每 5 秒被调用一次，是否该清理由 `plugin.c` 自己判断。
  定时模式按 `interval` 秒；阈值模式在占用超过 `threshold` 时清理，且两次自动清理
  至少间隔 60 秒（防止清完仍超阈值导致连续清理）。
- **手动清理**的 15 秒防抖由主程序按清单里的 `cooldown` 执行，插件不用管。
- 清理会清空**所有进程**的工作集，包括 Telegram 自己，清完几秒内客户端会略卡，属正常。

## API 一览（内部，供 `plugin.c` 使用）

| 函数 | 说明 |
|---|---|
| `MC_IsElevated()` | 当前进程是否已提权 |
| `MC_Initialize()` | 启用所需特权，清理前调用一次 |
| `MC_GetSupportedMask()` | 当前系统支持的区域掩码 |
| `MC_Clean(mask, &result)` | 执行清理，返回是否至少一项成功 |
| `MC_GetMemoryPercent()` | 当前物理内存占用百分比 |
