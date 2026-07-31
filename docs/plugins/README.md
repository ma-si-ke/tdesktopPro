# 插件开发说明

一个插件 = 一个文件夹，里面放 `plugin.json` 和一个 DLL。主程序按清单渲染界面、调度定时任务，
**新增插件不需要改主程序**。

```
plugins/
  memclean/
    plugin.json
    memclean.dll
```

安装记录在 `tdata/plugins.json`，插件设置在 `tdata/plugin-settings.json`，都由主程序维护。
手动放进 `plugins/` 的文件夹**不会被加载**，启动时会被清理——插件只能从市场安装。

---

## 一、DLL 接口（4 个导出符号，全部 `__stdcall`）

```c
int  __stdcall Plugin_Abi(void);
int  __stdcall Plugin_Init(const char *configJson, char *out, int outCapacity);
int  __stdcall Plugin_Call(const char *action, const char *argsJson,
                           char *out, int outCapacity);
void __stdcall Plugin_Shutdown(void);
```

- `Plugin_Abi` 返回 `1`，与主程序不符则插件不加载。
- 所有字符串都是 **UTF-8 JSON**。
- **缓冲区由主程序分配**（初始 8 KB）。返回值语义：
  - `>= 0`：写入的字节数
  - `< 0`：缓冲不够，需要 `-返回值` 字节，主程序会扩容重试（上限 1 MB）
  - 插件**不得**自行 malloc 后交给主程序释放（跨 DLL 堆会崩）

### `Plugin_Init`

加载后调用一次，用于自检并声明是否可用：

```json
{"available": false, "reason": "需要以管理员身份运行本程序。"}
```

`available` 为 false 时，主程序把该插件的所有控件置灰并显示 `reason`。省略 `available` 视为可用。

### `Plugin_Call`

`action` 是清单里写的动作名。传入的 `argsJson` 恒为：

```json
{"settings": {"auto": true, "mode": 1, "interval": 300}}
```

即清单声明的**全部设置项的当前值**——插件不需要自己存配置。

返回值可含以下字段，全部可选：

```json
{
  "toast": "内存清理完成，释放了 1.9 GB。",
  "error": "初始化失败。",
  "text": {"statusLine": "当前内存占用：42%"},
  "refresh": true
}
```

- `toast` → 弹出提示条；`error` → 弹出提示框（二选一，`error` 优先）
- `text` 的键对应清单里 `bind` 的标签，用于动态刷新文字
- 两个保留动作：`status`（打开插件页面时调用一次，用来填 `bind` 标签）、
  `tick`（定时调用，见下）

---

## 二、plugin.json

```json
{
  "manifest": 1,
  "name": "memclean",
  "title": "内存清理",
  "version": 2,
  "library": "memclean.dll",
  "description": "清理系统内存，低配电脑或多开用户尤为好用。",

  "settings": [
    { "id": "auto",      "type": "bool", "default": false },
    { "id": "mode",      "type": "int",  "default": 0 },
    { "id": "interval",  "type": "int",  "default": 300, "min": 30, "max": 86400 },
    { "id": "threshold", "type": "int",  "default": 80,  "min": 20, "max": 95 }
  ],

  "ui": [
    { "type": "button", "text": "立即清理", "action": "clean",
      "async": true, "cooldown": 15 },
    { "type": "label",  "bind": "statusLine", "text": "正在获取内存占用…" },
    { "type": "divider" },
    { "type": "toggle", "text": "自动清理", "setting": "auto" },
    { "type": "radio",  "setting": "mode", "visibleWhen": { "auto": true },
      "options": [ { "text": "定时清理", "value": 0 },
                   { "text": "按内存占用清理", "value": 1 } ] },
    { "type": "number", "text": "清理间隔（秒）", "setting": "interval",
      "visibleWhen": { "auto": true, "mode": 0 } },
    { "type": "number", "text": "内存占用阈值（%）", "setting": "threshold",
      "visibleWhen": { "auto": true, "mode": 1 } }
  ],

  "tray": [ { "text": "清理内存", "action": "clean", "async": true, "cooldown": 15 } ],
  "tick": { "everySeconds": 5 }
}
```

### 字段说明

| 顶层字段 | 必填 | 说明 |
|---|---|---|
| `manifest` | ✅ | 固定为 `1` |
| `name` | ✅ | 只允许 `a-z0-9_-`，必须与文件夹名、市场条目名一致 |
| `title` | | 界面显示名，缺省用 `name` |
| `version` | ✅ | 正整数，必须与市场条目的 `version` 一致 |
| `library` | ✅ | DLL 文件名，必须是纯文件名且以 `.dll` 结尾 |
| `settings` | | 设置项定义，最多 64 条 |
| `ui` | | 界面行，最多 128 行 |
| `tray` | | 托盘菜单项，最多 8 条 |
| `tick` | | `{"everySeconds": N}`，N 取 1–3600 |

**设置项**：`type` 为 `bool` / `int` / `text`；`int` 可带 `min` / `max`，主程序负责越界钳制。

**界面行类型**：

| type | 必填字段 | 说明 |
|---|---|---|
| `button` | `text`、`action` | 可加 `async`（工作线程执行）、`cooldown`（秒，按钮置灰倒计时） |
| `toggle` | `setting`（bool） | 开关 |
| `radio` | `setting`（int）、`options` | 单选，`options` 为 `{text, value}` 数组 |
| `number` | `setting`（int） | 数字输入框，上限取设置项的 `max` |
| `text` | `text` 或 `bind` | 普通文字 |
| `label` | `text` 或 `bind` | 分隔线下的灰色说明文字 |
| `divider` | — | 分隔线 |

**`visibleWhen`**：键为设置 id，值为 bool 或 int；全部匹配时该行才显示。

**耗时操作必须标 `async: true`**——同步动作在主线程执行，超过约 10 ms 就会卡界面。

---

## 三、打包与发布

插件包是 **zip**，根目录直接放 `plugin.json` 和 DLL（不要多套一层文件夹）：

```
memclean-v2.zip
  ├── plugin.json
  └── memclean.dll
```

上传到发布服务器后，在 `https://td.kakaco.top/api/plugins` 的列表里加一条：

```json
{
  "name": "memclean",
  "version": 2,
  "link": "https://td.kakaco.top/pl/memclean/2/memclean-v2.zip",
  "sha256": "<zip 的 sha256>",
  "size": <zip 字节数>,
  "released_at": "2026-07-31 12:00:00",
  "description": "…"
}
```

主程序下载后依次校验：`size` → `sha256` → 解压（拒绝含 `..` 等越界路径的条目）→
包内 `plugin.json` 的 `name` / `version` 必须与订单一致 → `library` 指向的文件必须存在。
任一步失败就整体丢弃。

---

## 四、生效时机

| 操作 | 何时生效 |
|---|---|
| 安装 | 下次启动 |
| 更新 | 下次启动（新版本先落在 `<name>.new/`，运行中的 DLL 无法替换） |
| 删除 | 下次启动（先标记，重启时才真正删除文件夹） |

启动时主程序会对账：记录在但文件夹没了 → 删记录；文件夹在但没记录 → 删文件夹。

---

## 五、已知限制

- 只能用上面列出的控件，复杂界面仍需主程序开发
- 插件访问不到 Telegram 的消息、联系人等数据
- 插件与主程序同进程，插件崩溃会导致 Telegram 崩溃
- 仅 Windows
