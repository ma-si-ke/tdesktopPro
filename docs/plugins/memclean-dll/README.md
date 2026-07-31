# memclean

独立的 Windows 内存清理 DLL。核心技术来自 [Mem Reduct](https://github.com/henrypp/memreduct)（(c) Henry++，GPL-3.0），
本实现为自包含移植：不依赖 routine 库，只依赖 ntdll / kernel32 / advapi32。

**License: GPL-3.0-or-later**（继承自 Mem Reduct 的实现思路与掩码设计）。

## 构建

MSVC 一行编译：

```bat
cl /nologo /W4 /O2 /utf-8 /DUNICODE /D_UNICODE /LD memclean.c /link /OUT:memclean.dll
```

或 CMake：

```bat
cmake -B build && cmake --build build --config Release
```

## 使用（C / C++）

```c
#include "memclean.h"
#pragma comment (lib, "memclean.lib")

if (MC_IsElevated () && MC_Initialize ())
{
    MC_RESULT result;

    if (MC_Clean (MC_MASK_DEFAULT, &result))
        printf ("freed %llu bytes\n", result.freed_bytes);
}
```

## 使用（C# P/Invoke）

```csharp
[StructLayout(LayoutKind.Sequential)]
struct McResult
{
    public ulong FreedBytes;
    public uint SucceededMask;
    public uint FailedMask;
    public uint SkippedMask;
    public int LastStatus;
}

[DllImport("memclean.dll")] static extern bool MC_IsElevated();
[DllImport("memclean.dll")] static extern bool MC_Initialize();
[DllImport("memclean.dll")] static extern uint MC_GetSupportedMask();
[DllImport("memclean.dll")] static extern bool MC_Clean(uint mask, out McResult result);
[DllImport("memclean.dll")] static extern uint MC_GetMemoryPercent();

const uint MC_MASK_DEFAULT = 0xE7; // 见 memclean.h
```

## 必读注意事项（来自 Mem Reduct 的实践经验）

1. **必须以管理员运行**，且调用 `MC_Clean` 前先调 `MC_Initialize()` 启用
   `SeProfileSingleProcessPrivilege` 和 `SeIncreaseQuotaPrivilege`，否则 NT API 返回
   `STATUS_PRIVILEGE_NOT_HELD`。
2. **不要在自动/定时清理里使用 `MC_STANDBYLIST` 和 `MC_MODIFIEDLIST`**。
   清空整个待命列表等于丢弃全部磁盘缓存，清完系统会因缓存未命中而变卡；
   Mem Reduct 默认也排除这两项（`MC_MASK_DEFAULT` 同样不含）。
3. **自动清理请加冷却时间**（Mem Reduct 用 30 秒）：内存长期高于阈值时，
   没有冷却会陷入"清理→无效→再清理"的循环。
4. 各区域独立容错：某一项失败不会中断其余项，失败详情在 `MC_RESULT.failed_mask`
   和 `last_status` 里。
5. 不支持当前系统版本的区域（注册表缓存需 Win8.1+，内存合并需 Win10+）会自动跳过，
   记录在 `skipped_mask`。

## API 一览

| 函数 | 说明 |
|---|---|
| `MC_IsElevated()` | 当前进程是否已提权 |
| `MC_Initialize()` | 启用所需特权，清理前调用一次 |
| `MC_GetSupportedMask()` | 当前系统支持的区域掩码 |
| `MC_Clean(mask, &result)` | 执行清理，返回是否至少一项成功 |
| `MC_GetMemoryPercent()` | 当前物理内存占用百分比 |
