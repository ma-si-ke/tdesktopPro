# 本地备忘录系统设计

> **状态**：草案，待实现
> **作者**：Claude（基于 2026-07-28 调研 session）
> **目的**：在第三栏（信息面板位置）提供一套本地备忘录系统，形态为「类聊天页面」：有独立输入框可发送文字/图片/文件/语音/媒体组，消息可右键编辑删除；双击消息可把内容快捷填充到当前打开的聊天（不直接发送）。顶部支持多个「文件夹」，每个文件夹是一个独立的备忘录聊天。
> **数据去向**：纯本地（`tdata/memo/`），不上传服务器。数据结构预留云端上传/下载能力，本期不实现同步逻辑。

---

## 0. 决策汇总

| 维度 | 决策 | 理由 |
|---|---|---|
| 面板载体 | 独立 `Window::SectionWidget` 占用第三栏 | 先例 `ChatHelpers::TabbedSection`；Info 框架是纯滚动列表模型，塞输入框别扭 |
| 与信息面板关系 | 互斥（设置标志三向互斥） | 复用 `thirdSectionInfoEnabled` / `tabbedSelectorSectionEnabled` 已有的互斥模式 |
| 类聊天页组装 | `ListWidget` + `ComposeControls` | 整体照抄 `settings_shortcut_messages.cpp` |
| 消息载体 | 纯本地 `HistoryItem`，由自己的容器持有 | 照抄 `Data::ShortcutMessages` + `EphemeralMessages` 的本地构造范式 |
| 数据存储 | 纯本地，每文件夹一个自包含目录 | 为「整文件夹上传/下载」预留；`local-notes` 已验证该存储模式 |
| 文件夹标签栏 | `Ui::SubTabs`（药丸式） | 自带横向滚动+边缘渐隐+拖拽重排，适配 292–392px 窄栏 |
| 双击填充 | 重新构造内容，**不走转发** | 本地消息无服务端 id，`allowsForward()` 天然为 false，转发协议走不通 |
| 语音填充 | 确认框 + `sendVoiceMessage` | 无「语音草稿」机制；复用 fork 已有的 OGG→语音转换 |
| 云同步 | 本期只留数据结构口子 | 用户明确要求后期再做 |

---

## 1. 架构总览

```
顶栏 _memoToggle（信息按钮旁）
  └─ Core::Settings::thirdSectionMemoEnabled（与 Info / Tabbed 三向互斥）
       └─ 第三栏 MemoSection（Window::SectionWidget）
            ├─ Ui::SubTabs                  ← 文件夹标签（顶部）
            ├─ HistoryView::ListWidget      ← 消息列表（完整聊天渲染 + 右键菜单）
            ├─ HistoryView::ComposeControls ← 输入框（文字/图片/文件/语音/媒体组）
            └─ Data::MemoMessages           ← 本地数据层 + 持久化

双击消息 → 文字：本地草稿填充当前聊天输入框
         → 媒体/媒体组/文件：预填 SendFilesBox（用户确认才发送）
         → 语音：确认框 → sendVoiceMessage
```

### 1.1 文件布局

新增文件（全部加进 `Telegram/CMakeLists.txt`）：

| 文件 | 职责 |
|---|---|
| `data/data_memo_messages.cpp/h` | 文件夹与消息模型、运行时容器、HistoryItem 重建 |
| `data/data_memo_storage.cpp/h` | 本地持久化（index/manifest/files 读写、序列化格式） |
| `memo/memo_section.cpp/h` | 第三栏 section（SectionWidget + Memento + 标签栏 + list + compose） |
| `memo/memo_fill.cpp/h` | 双击填充逻辑（文字草稿 / SendFilesBox 预填 / 语音发送） |
| `settings/sections/settings_memo_folders.cpp/h` | 文件夹管理设置页 |
| `memo/memo.style` | 面板与标签栏样式 |

---

## 2. 数据结构

### 2.1 磁盘布局

```
tdata/memo/<accountUserId>/
├── index.dat                 ← 文件夹清单
└── <folderId>/               ← 每个文件夹自包含（同步单元）
    ├── manifest.dat          ← 该文件夹全部消息元数据
    └── files/<uuid>.<ext>    ← 媒体原始文件
```

**自包含目录**是为云同步预留的核心设计：上传 = 打包该目录，下载 = 解包后分配新的本地 folderId。目录内部一律使用相对文件名，不存绝对路径。

### 2.2 模型

```cpp
struct MemoFolder {
    uint64 id;              // 本地随机稳定 id（非自增，避免多端合并冲突）
    QString title;
    int order;
    TimeId created, updated;
    uint64 revision;        // 每次改动 +1，云同步冲突检测
    QString cloudId;        // 上传后服务端 id；空 = 从未同步（本期仅占位）
};

struct MemoMedia {
    enum class Type { Photo, File, Voice, Audio, Video };
    Type type;
    QString file;           // files/ 下的相对文件名
    QString originalName;   // 原始文件名（重发时用）
    QString mime;
    int64 size;
    QSize dimensions;
    crl::time duration;     // 语音/音频/视频
    QByteArray waveform;    // 语音波形
    bool spoiler;
};

struct MemoMessage {
    uint64 id;              // 文件夹内递增 → 映射运行时本地 MsgId
    TimeId date, editDate;
    TextWithEntities text;  // 正文，或媒体描述（带格式实体）
    uint64 groupId;         // 0 = 单条；相同值 = 同一媒体组
    std::optional<MemoMedia> media;
    uint64 revision;
};
```

### 2.3 云同步预留原则（本期必须遵守）

1. **所有 id 用随机/稳定值**，不用顺序号 —— 多端合并不冲突
2. **每条记录带 `revision` + 时间戳** —— 支持增量同步与冲突检测
3. **文件夹目录自包含、内部全相对路径** —— 目录即同步单元
4. **序列化带格式版本号**，新字段只追加在尾部（遵守 `AGENTS.md` 的序列化规则）

---

## 3. 运行时消息层

### 3.1 需新增的枚举

| 新增 | 位置 | 作用 |
|---|---|---|
| `Context::LocalMemo` | `history/view/history_view_element.h` | 驱动右键菜单与渲染行为 |

**这是对公共代码唯一的枚举改动。** 原设计想新增 `MessageFlag::LocalMemo` 和专属 MsgId 段，实际检查后两者都不可行也不必要：

1. **`MessageFlag` 的 64 位已全部占满**（`Ephemeral` 占用最后的第 63 位），无位可加。改用已有的 `MessageFlag::FakeHistoryItem`（第 31 位）—— 它在 `FinalizeMessageFlags`（`history_item_helpers.cpp:1172-1178`）里的作用正是阻止消息获得 `HistoryEntry`，语义完全吻合。备忘录消息 flags = `FakeHistoryItem | Local | Outgoing | HasFromId`。

2. **MsgId 号段需要避让一个无上界分配器**：`Data::Session::nextNonHistoryEntryId()`（`data_session.h:255`）从 `ShortcutMaxMsgId` 起**无上界递增**，服务于所有「不进历史的假消息」（聊天背景预览、待办预览、配色预览等）。因此备忘录号段起点设在 `ShortcutMaxMsgId + (1LL << 40)`，留出约 10^12 的间隔，两个分配器实际不可能相遇：

```cpp
constexpr auto MemoMinMsgId = MsgId(ShortcutMaxMsgId + (1LL << 40));
constexpr auto MemoMaxMsgId = MsgId(MemoMinMsgId + (1LL << 32));
[[nodiscard]] constexpr inline bool IsMemoMsgId(MsgId id) noexcept;
```

3. **身份识别**：`HistoryItem::isLocalMemo()`（`history_item.cpp`）= `IsMemoMsgId(id)`，与 `isBusinessShortcut()` 同风格，constexpr 判断、零耦合。

**因此需要放行的权限检查**（否则右键菜单无编辑、消息无法删除）：

| 位置 | 改动 |
|---|---|
| `HistoryItem::canBeEdited()` | 首条排除式加 `&& !isLocalMemo()` |
| `HistoryItem::canDelete()` | `!isHistoryEntry() && ...` 链加 `&& !isLocalMemo()` |
| `HasEditMessageAction()`（`history_view_context_menu.cpp`） | 允许的 Context 列表加 `Context::LocalMemo` |

`isTooOldForEdit()` 因宿主 peer 为 self（`canEditMessagesIndefinitely()`）而不生效，备忘录消息永久可编辑。

**对既有功能的影响：无。** 快捷回复使用 `ScheduledMaxMsgId ~ ShortcutMaxMsgId` 号段与 `ShortcutMessage` 标志，本方案两者均不触碰。备忘录消息虽以收藏夹 `History` 为宿主容器，但因带 `FakeHistoryItem` 而不会成为 `HistoryEntry`，不会显示在收藏夹聊天中 —— 快捷回复当前即以同样方式挂在收藏夹上（`data_shortcut_messages.cpp:118`），是已上线验证的行为。

### 3.2 容器

`Data::MemoMessages` 照抄 `data_shortcut_messages.cpp` 的分桶结构（快捷回复本就是「按 shortcutId 分桶的多组消息」，与多文件夹一一对应）：

```cpp
using OwnedItem = std::unique_ptr<HistoryItem, HistoryItem::Destroyer>;
struct List {
    std::vector<OwnedItem> items;
    base::flat_map<uint64, not_null<HistoryItem*>> itemById;
};
base::flat_map<uint64 /*folderId*/, List> _data;
rpl::event_stream<uint64> _updates;
```

另需 `base::flat_map<not_null<HistoryItem*>, uint64> _itemToFolder` 反查表，供按 item 定位所属文件夹（编辑/删除时改写对应 manifest）使用。

- 宿主 History：`owner->history(session->userPeerId())`（收藏夹），与快捷回复同法；消息因带 `FakeHistoryItem` 而不进入任何会话
- `listSource()` 忽略 `aroundId`/`limit`，每次吐全量 `MessagesSlice`（`skippedBefore/After = 0`）
- item 构造照抄 `EphemeralMessages::applyNew` 的 `history->addNewLocalMessage(...)` 范式，id 取自备忘录号段（§3.1），flags = `FakeHistoryItem | Local | Outgoing | HasFromId` → `isRegular()` 为 false → 转发等服务端动作入口自动禁用，同时渲染为右侧发出气泡
- 运行时 MsgId 与持久化 id 分离：manifest 存稳定的 `MemoMessage.id`，加载时映射到号段内的运行时 MsgId，保证云同步 id 不依赖运行时状态

### 3.3 媒体消息重建（已知最高风险项）

启动时从 manifest 重建带媒体的 HistoryItem：

- **文件/语音/音频/视频**：构造 `DocumentData` 后 `setLocation(本地路径)`，可直接从磁盘渲染
- **照片**：照片在 tdesktop 里是「缓存字节」而非独立文件，需把字节注入媒体缓存，或统一按 document-as-image 处理

> 实现时必须先做小实验验证渲染路径，再铺开。

### 3.4 删除

**`data/data_histories.cpp` 无需改动。** 备忘录消息在删除分流链中会跳过 saved-music / scheduled / shortcut 三个分支，落到 `remove` 列表；因 `isRegular()` 为 false 而不会被加入 `idsByPeer`，故不产生任何服务器请求，最终只执行本地 `item->destroy()`。

容器侧订阅 `Data::Session::itemRemoved()` 完成清理：从 `List` 与反查表移除、改写 manifest、删除孤立媒体文件。

---

## 4. 第三栏面板

### 4.1 骨架

照 `chat_helpers/tabbed_section.h` 写 `MemoMemento : Window::SectionMemento` + `MemoSection : Window::SectionWidget`（`showInternal` 返回 false、`forceAnimateBack` 返回 true、`removeRequests()` 驱动关闭）。

内部组合照 `settings_shortcut_messages.cpp`：`ListWidget`（`overrideChatMode(Default)`）+ `ComposeControls`（`stOverride = &st::repliesComposeControls`，关闭 sendAs/ttlInfo/attachBotsMenu 等特性）+ 独立 `Ui::ChatStyle`/`ChatTheme` + `CornerButtons`。

### 4.2 文件夹标签栏

`Ui::SubTabs`（`ui/controls/sub_tabs.h`），用法照 `info/stories/info_stories_inner_widget.cpp:757-853`：

- `setTabs({id, text})` ← 文件夹列表；`activated()` → 切换当前文件夹；`contextMenuRequests()` → 重命名/删除
- 末项固定「+ 新建」，`setPinnedInterval` 钉住
- `reorderUpdates()` → 落盘新 order
- ⚠️ 它的几何/颜色硬编码引用 `credits.style` 的 `giftBox*` 全局常量，改动会影响礼物商店页面 → 需在 `memo.style` 新增独立常量组并小改 `sub_tabs.cpp`

### 4.3 发送流程

全部走本地：不调 `api().sendMessage`，改为「媒体文件复制进 `files/` → 写 manifest → 构造本地 item」。

- 文字：`ComposeControls::sendRequests`
- 语音：`sendVoiceRequests` 给出 `bytes + waveform + duration`，原样存（对应 `MemoMedia.waveform/duration`），OGG 落盘
- 图片/文件/媒体组：`attachRequests` → `SendFilesBox` → 确认回调按组落盘（照抄 `sendingFilesConfirmed` 的 `bundle->groups` 循环，同组共享 `groupId`）
- OGG 音频文件按语音发送：复用 fork 已有的 `IsSingleOggAudio()` + `::Media::ConvertToVoiceMessage()`（`settings_shortcut_messages.cpp:334`）

### 4.4 右键菜单

菜单内容由 `listContext()` 返回的 `Context` 驱动。`Context::LocalMemo` 需在 `history_view_context_menu.cpp` 的编辑/删除白名单里放行（照 `Context::ShortcutMessages` 的处理），并确保转发/回复/举报等服务端动作不出现（`isRegular()` 已天然挡掉大部分）。

编辑走 `editMessageRequested` → `ComposeControls::editMessage`，保存时改写 manifest；替换媒体走 `EditCaptionBox::StartMediaReplace`。

---

## 5. 设置标志与顶栏联动

### 5.1 设置标志

`Core::Settings` 新增 `_thirdSectionMemoEnabled` + `_thirdSectionMemoEnabledValue`，照 `core_settings.cpp:1300-1322` 的模式做**三向互斥**：`setThirdSectionMemoEnabled(true)` 时关掉 Info 与 Tabbed；同时在那两个已有 setter 里关掉 memo。

序列化按 `AGENTS.md` 规则处理（优先用 KV prefs facility 避免二进制流顺序问题）。

### 5.2 必须修改的四处（漏了会出 bug）

| 位置 | 改动 | 不改的后果 |
|---|---|---|
| `window_session_controller.cpp` `useNormalLayout()` | 条件加 `&& !thirdSectionMemoEnabled()` | 三栏布局不启用，面板根本打不开 |
| `mainwidget.cpp` `updateThirdColumnToCurrentChat()` | memo 开启时 early-return | **每次切聊天备忘录被 Info 顶掉** |
| `mainwidget.cpp` `updateControlsGeometry()` 补内容链 | 加 memo 分支 | 窗口变宽后第三栏不会恢复备忘录 |
| `resizeForThirdSection()` / `closeThirdSection()` | 标志 save/restore 与置 false 带上 memo | 窗口宽度回缩错误、关闭后状态残留 |

### 5.3 顶栏按钮

`history_view_top_bar_widget.{h,cpp}` 照 `_infoToggle` 成对添加 `_memoToggle`：构造、click callback（`toggleMemoSection()`）、`updateControlsGeometry()` 的 `_rightTaken` 排布、`updateControlsVisibility()` 可见性、订阅 `thirdSectionMemoEnabledValue()` 做激活高亮。样式 `topBarMemo` / `topBarMemoActive` 新增图标。

---

## 6. 双击填充

### 6.1 钩子

`ListWidget` 是 `final` 不可继承 → 给 `ListDelegate` 加虚函数（本代码库标准做法）：

```cpp
virtual bool listDoubleClicked(not_null<HistoryItem*> item) { return false; }
```

在 `ListWidget::mouseDoubleClickEvent`（`history_view_list_widget.cpp:3272`）顶部、`trySwitchToWordSelection()` 之前调用；返回 true 则 `mouseActionCancel()` 并 return（屏蔽选词与 QuickAction）。

当前聊天用 `controller->activeChatCurrent().thread()` 获取。

### 6.2 各类型分流

| 类型 | 做法 |
|---|---|
| 纯文字 | `PrepareEditText(item)` → 照 `Data::SetChatLinkDraft`（`data_drafts.cpp:188-219`）设 `setLocalDraft` + `clearLocalEditDraft` + fire `EntryUpdate::Flag::LocalDraftSet`；UI 刷新由框架自动完成 |
| 单图/视频 + 描述 | `Storage::PrepareMediaList({path},...)` → `SendFilesBoxDescriptor{.list, .caption, ...}`（照 `window_session_media.cpp:88-121`） |
| 媒体组 | 同 `groupId` 的文件按序装进一个 `PreparedList`；**描述分两层** ↓ |
| 文件 | 同上，`overrideSendImagesAsPhotos = false` 锁定「以文件发送」，避免存的是文件发出去变压缩图 |
| 语音 | `MakeConfirmBox` 确认 → `api().sendVoiceMessage(bytes, waveform, duration, ...)` |

**媒体组描述分层规则**（关键实现细节）：`SendFilesBox` 确认时会把主描述框内容**无条件覆盖**到「描述位」文件的 caption（照片视频组为第一个文件，见 `send_files_box.cpp:2505-2510`）。因此预填时必须：

- 描述位那条消息的描述 → 放进 `SendFilesBoxDescriptor.caption`（主描述框）
- 其余各条的描述 → 放进各自的 `PreparedFile::caption`

两边互补，既不丢也不重。

语音不做预填而用确认框，因为语音没有可编辑的预览需求，确认框语义最清楚（备选方案：当文件塞进 `SendFilesBox`，确认回调里检测 OGG 转语音，但 box 里显示为文件行、与语音气泡观感不一致）。

---

## 7. 设置页

- **入口**：`settings/sections/settings_more_features.cpp`（整文件在 `TDESKTOP_EMPLOYEE_MODE` 宏内）加 `Ui::SettingsButton` → `showOther(MemoFoldersId())`
- **文件夹管理页**：照抄 `settings/business/settings_quick_replies.cpp`（239 行）：`Section<T>` 模板、`addWrap`（新建按钮）+ `inner`（列表）两段式、差量刷新
- **新建/重命名**：复用 `EditShortcutNameBox` 同款输入名字 box（非空 name 即重命名模式）
- **删除**：`Ui::MakeConfirmBox` + `attentionBoxButton`
- **排序**：`Ui::VerticalLayoutReorder` + `setMouseEventProxy`（拖拽手柄），落盘照 `settings_information.cpp:1117-1141`

---

## 8. 实施顺序与风险

| 阶段 | 内容 | 风险 |
|---|---|---|
| 1 | 枚举/标志位基础（MsgId 段、MessageFlag、Context） | 低 |
| 2 | `Data::MemoMessages` + 持久化 | **高**（媒体重建） |
| 3 | `MemoSection` 面板（标签栏 + list + compose） | 中 |
| 4 | 设置标志互斥 + 顶栏按钮 + MainWidget 接线 | 中（5.2 四处坑） |
| 5 | 双击填充 | 中 |
| 6 | 设置页 | 低 |
| 后期 | 云端上传/下载文件夹 | 本期不做 |

复用度总览：六大块中五块有高复用模板，真正的新代码集中在**本地持久化层 + 媒体消息重建**。

---

## 9. 编码约定提醒

遵守 `AGENTS.md`：不写解释性注释；用 `auto` 与 `u"..."_q`；尺寸一律进 `.style` 不硬编码；动画时长用 `constexpr auto kName = crl::time(N)`；序列化新字段只追加尾部；Windows 下 CRLF + UTF-8 无 BOM；提交信息一行简洁主题、不加任何 AI 署名。
