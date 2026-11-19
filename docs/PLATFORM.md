# 南都工作平台（ND_DrawingManagement）现状文档

> 这份文档是**项目单一事实源**（Single Source of Truth）。任何接手本工程的 AI 先读这里，再动代码。发现不一致请先更正本文档，再改代码。
> 最后更新：2026-11-19（b33：接管 WinHttp 的 COM 异常，服务器连不上时弹中文友好提示而非 AutoIt 原生错误框；HTTP 每个 COM 调用补 @error 检查；启动刷新失败即提示）

---

## 1. 项目定位

**历史**：最初是「图纸管理平台」，单一 Qt/C++ 桌面端，管产品图纸的录入/查询/图纸归档。

**现状**：已升级为「南都工作平台」，多端协同：

| 端 | 语言/技术 | 目录 | 角色 |
|---|---|---|---|
| PC 主程序 | Qt 6.8.3 / C++17 / CMake | `ND_DrawingManagement_PC/` | **权威数据源**：唯一写入点，局域网 HTTP 服务端口 8080 |
| 手机端 | Kotlin / Android | `ND_DrawingManagement_Android/` | 离线只读 + 增量同步，管端子线束库存、扫码入库 |
| 下线机 | AutoIt 3 | `ND_DrawingManagement_Device/` | 现场设备：通过 HTTP 取 PC 端下发的下线任务 → 执行 → 回写结果 + 打印标签 |

根目录 `build/` 是旧版残留的构建目录（用旧源码时生成），**新构建全部走 `ND_DrawingManagement_PC/CMakeLists.txt`**。

---

## 2. PC 端模块现状（ND_DrawingManagement_PC/）

### 2.1 构建
- `CMakeLists.txt`：Qt 6.8.3 + C++17
- 编译选项已加 UTF-8（MSVC `/utf-8`，GNU `-finput-charset -fexec-charset`）
- 运行时资源目录：`exe 同级的 data/`（SQLite 数据库 nd_drawings.db）、`exe 同级的 ocr/PaddleOCR-json/`（PaddleOCR-json v1.4.1 离线引擎）

### 2.2 一级 / 二级导航（mainwindow.cpp）
- 一级菜单 `m_menuGroup`：0图纸 / 1线束 / 2工具 / 3设置（网页为外链不入组）
- 二级子栏 `m_subBarStack`：0图纸子栏 / 1线束子栏

### 2.3 页面栈 `m_stack`（8 页）

| 页索引 | 所属板块 | 类 | 文件 | 入口 |
|---|---|---|---|---|
| 0 | 图纸 | MainWindow 内嵌 body（主界面 + 分类 Tab + 搜索 + 列表 + 图纸预览） | mainwindow.cpp | 菜单"图纸查询" |
| 1 | 图纸 | EntryDialog | ui/entrydialog.cpp | 菜单"录入" |
| 2 | 图纸 | ManagePage | ui/managepage.cpp | 菜单"管理"（管理员验证） |
| 3 | 全局 | SettingsDialog | ui/settingsdialog.cpp | 菜单"设置" |
| 4 | 工具 | ToolsPage | ui/toolspage.cpp | 菜单"工具" |
| 5 | 线束 | MoldTerminalPage（模具端子录入） | ui/moldterminalpage.cpp | 菜单"模具端子录入" |
| 6 | 线束 | CutTaskPage（下线任务） | ui/cuttaskpage.cpp | 菜单"下线任务" |
| 7 | 线束 | MoldQueryPage（模具端子查询） | ui/moldquerypage.cpp | 菜单"模具端子查询" |

其他独立窗口：
- OCR 图片拖入框 `OcrImageDrop`：工具栏里的缩略图，拖图进去直接 OCR → 多型号搜索（ui/ocrimagedrop.cpp）
- OCR 结果浮层 `OcrResultPopup`：右侧半透明面板（ui/ocrresultpopup.cpp）
- 登录遮罩 `OverlayMask`：账号密码卡片（ui/overlaymask.cpp）
- 统一提示窗 `NdMessageBox`：黑色半透明蒙版 + 深色圆角卡片，**所有 QMessageBox 已替换**（ui/ndmessagebox.cpp）
- 版本更新 Dialog：ui/updatedialog.cpp
- 更改记录 Dialog：ui/changelogdialog.cpp
- 角色管理 Dialog：ui/roledialog.cpp
- 物料编码查询 Dialog：ui/materialcodedialog.cpp

### 2.4 数据库表（db/databasemanager.cpp）

基础表（原图纸平台）：
- `product`：产品主表，分类 + 型号唯一
- `drawing`：图纸路径、类型（下发图/会签图）、图纸时间、归档路径（原文件）
- `product_ext`：分类特有字段 KV
- `field_config`：各分类特有字段定义（field_key → 中文标签、显示顺序）
- `change_log`：更改记录（产品级，填写的"更改说明"自动追加）
- `user`：账号密码、角色（admin/user/viewer）

扩展示表（本次迭代新增）：
- `app_meta`：应用元信息（当前 PC 安装包版本、Android APK 版本）
- `product_qty_log`：每日结存数量日志（手机端库存页算用量/趋势图）
- `op_log`：用户操作日志（登录、写操作）
- `stock_batch` / `stock_archive`：库存批次/归档
- `chip_item`：芯片/端子主数据
- `mold_item`：模具端子主数据（分类、型号、端子编号、Pin 数、物料编码等）
- `cut_harness`（旧库 `cut_batch` 自动迁移，name 列存表格型号、batch_no 存表格批次号）/ `cut_item` / `cut_task` / `cut_machine` / `cut_rule` / `cut_device` / `cut_device_state` / `cut_print_log`：下线机业务一整套

### 2.5 PC 端样式与设备端界面现状（2026-11-19）
- `mainwindow.cpp` 的 `#darkMenuTabs` QSS 已补全：`QTabBar`、`QTabBar::tab`、`QStackedWidget` 均使用深色背景；`toolspage.cpp` / `settingsdialog.cpp` 的 `QTabWidget` 与 `tabBar()` 均开启 `WA_StyledBackground`，修复工具页/设置页二级菜单右侧背景与内容区不一致。
- 设备端 `CutDevice.au3` 当前为右侧竖长浮窗（430×560，贴桌面右下角，`WS_POPUP + TOPMOST + TOOLWINDOW`），主窗口自适应桌面尺寸，三个自绘标签按钮（全部/未完成/已完成）+ 底部完整详情区；按钮绿色（开始/打印）、红色（已完成）、中性灰（其余），确认文案统一为「已完成」。
- 设备设置页已移除机台名称输入，只保留机台号下拉；机台名由 `_MachineName()` 按 1~5 生成（一号机…五号机）。设置密码弹窗统一 `0x2B3046` 深灰背景，设置项文字统一白色。

---

## 3. HTTP 服务器（server/lanhttpserver.cpp，端口 8080）

### 3.1 只读网页（浏览器直接访问）
- `GET /` → 内嵌查询网页（HTML，深色风格，主界面/录入/管理/设置四屏）
- `GET /drawing?id=` → 图纸文件（图片/PDF 原样输出，浏览器预览）

### 3.2 手机端离线同步（HTTP GET，无鉴权）
- `GET /api/sync/schema` → 数据库 schema 版本号
- `GET /api/sync/meta` → 服务器时间 / 存储根路径
- `GET /api/sync/products?since=ISO时间` → 增量产品（since 空 = 全量，含 ext 字段）
- `GET /api/sync/drawings?since=ISO时间` → 增量图纸记录（含相对存储根的路径）
- `GET /api/sync/qty_logs?since=ISO时间` → 每日结存数量日志
- `GET /api/sync/categories` → 产品分类列表
- `GET /api/sync/fields` → 各分类特有字段定义
- `GET|HEAD /api/sync/file?path=相对路径[&i=N&total=M]` → 二进制图纸 / HEAD 仅返回大小

### 3.3 PC 客户端鉴权接口
- `POST /api/auth/login` → 校验账号密码，返回 token 与角色（token 仅内存保存，服务器重启后需重新登录）
- `POST /api/data` → 只读查询（action 分发，无需 token；含 `list_op_logs` 用户操作日志）
- `POST /api/write` → 写操作（action 分发，必须带有效 token，服务端统一落盘）

### 3.4 下线机设备接口（AutoIt 端调用）
- `POST /api/device/auth` → `machineNo + deviceKey` 鉴权，返回 token
- 读任务：`POST /api/data` action=`cut_query` what=`tasks`（按 `machineId` 过滤；PC 端还支持 batches/machines/rules）
- 写结果：`POST /api/write` action=`cut_write`；当前设备端实际使用 op=`start_task` / `confirm_task` / `log_print`，服务端另支持 `finish_task` / `commit_dispatch` / `retask` 等 PC 派工 op
- 检查更新：`POST /api/update/info` `kind=device` + `arch=x86|x64`，下载 `GET /api/update/device/package?arch=x86|x64`（免登录，按位数取对应包）

### 3.5 版本管理（三端统一机制）

后端统一：`PackageKind { Pc, Android, Device }`。**PC Qt 本体只构建/分发 64 位**；只有 AutoIt 下线机 exe 需要区分 `PackageArch { X86, X64 }`，两种位数各留最新一份、元数据分开。

**HTTP 接口**：
- `POST /api/update/info` — PC 设置页查询最新版本（body 带 `kind=pc|android|device`，device 可带 `arch=x86|x64`）
- `POST /api/update/upload` — PC 设置页上传安装包（分块 1MB，body 带 `kind` + `arch`）
- `GET  /api/update/package` — 下载 PC 安装包（不区分位数）
- `GET  /api/update/android/info` — 手机 APP 免登录查询
- `GET  /api/update/android/package` — 手机 APP 免登录下载 APK
- `GET  /api/update/device/info?arch=x86|x64` — **下线机 Device 免登录查询**（AutoIt 端调用）
- `GET  /api/update/device/package?arch=x86|x64` — **下线机 Device 免登录下载 exe**（AutoIt 端调用）

**元数据文件**（服务器 installer/ 目录）：
- `update.json` → PC 安装包元数据
- `update_android.json` → 手机 APK 元数据
- `update_device_x86.json` / `update_device_x64.json` → 下线机 32/64 位 exe 元数据（旧 `update_device.json` 只作兼容占位）
- 安装包文件名：`ND_DrawingManagement_PC.exe`（64 位）/ `ND_DrawingManagement.apk` / `CutDevice_yyyyMMdd-bXX-x86.exe` / `CutDevice_yyyyMMdd-bXX-x64.exe`

**版本号格式**：
- **PC 端**：`PackageKind::Pc` → 从 `app_meta` 表读 `version` 字段（如 `1.0.0`），不区分位数
- **手机 APK**：从文件名提取日期（`ND_DrawingManagement_v1.0_2026-11-09.apk` → `2026-11-09`），取不到用文件修改时间
- **下线机 Device**：`yyyyMMdd-bXX-x86` / `yyyyMMdd-bXX-x64`，完整版本号来自 exe 文件名；AutoIt 运行时 `$gAppVer = $gAppVerBase & "-" & $gAppArch`

**PC 设置页 UI**：版本管理 Tab 有 PC 安装包、手机 APK、**下线机 32 位**、**下线机 64 位** 四个上传区块（默认路径 `E:\HouYawei\01.QTProject\ND_DrawingManagement\ND_DrawingManagement_Device`）。旧客户端不带 `arch` 时，服务端默认按 **x86** 处理（现场 4 台 WinXP）。

---

## 4. 架构红线（AI 接手时必须遵守）

1. **服务器主机是唯一写入点**。PC 客户端（`/api/write`）、下线机（`/api/write`）都走 HTTP 接口写，不直接操作 SQLite。客户端连离线同步都只读 `product_qty_log` / `drawing` 的增量，不写入。
2. **数据库路径**：`exe 目录/data/nd_drawings.db`。图纸文件归档路径 `E:\HouYawei\0000图纸\<分类>\<型号>\<原文件名>`，保留原名、重名加时间戳。
3. **分类七类**：控制器 / 电器盒 / 开关 / 传感器 / 仪表 / 线束 / 方向盘。
4. **图纸类型两类**：下发图 / 会签图。OCR 识别到「外发」「下发」按下发图；「会签」按会签图；都没有按默认下发图。
5. **OCR 引擎**：PaddleOCR-json v1.4.1 离线（`ocr/PaddleOCR-json/PaddleOCR-json.exe -image_path=... -ensure_ascii=false`）。识别**只送右下角标题栏区域**（起点 45%，宽高不足 1800px 先放大到 1800），准确率已验证可接受。
6. **界面风格**：深色（#22293C 底 / #2B3046 面板）、自绘无边框窗口、QPainter 画窗口按钮、遮罩弹窗用 OverlayMask + NdMessageBox；所有提示窗必须用 NdMessageBox（已替换完 QMessageBox，残留 0 处）。
7. **Qt 版本**：当前构建环境是 Qt 6.8.3 MSVC 2022。MinGW 套件仅在命令行编译验证（用户本地 Qt Creator 用 MSVC）。

---

## 5. 跨端通信接口速查表

| 调用方 | 接口 | 目的 |
|---|---|---|
| 浏览器 | `GET /` | 只读查询网页 |
| 浏览器 | `GET /drawing?id=N` | 图纸预览 |
| 手机 APP | `/api/sync/*` 全部 | 离线同步产品/图纸/数量日志 |
| 手机 APP | `/api/update/android/info` + `/api/update/android/package` | 检查并下载 APK 更新（免登录） |
| PC 客户端（局域网） | `/api/auth/login` → `/api/write` | 写数据（增删改） |
| 下线机（AutoIt） | `/api/device/auth` → `/api/data` 读 `/api/write` 写 | 取任务 → 执行 → 回写 |
| **下线机（AutoIt）** | **`/api/update/device/info` + `/api/update/device/package`** | **检查并下载 exe 更新（免登录，b18+）** |
| 所有端 | `/api/update/info` / `/api/update/upload` | 服务器主机分发安装包（PC 设置页） |

---

## 6. 待处理 TODO（发现就写这里，其他 AI 共享）

1. **[ ] 入库数量 / 出库数量 在 UI 上的展示位置**：`product_qty_log` 已有表、手机端库存页在用，但 PC 主界面列表里还没加数量列。主界面"补充信息" Dialog 里也没有单独展示数量变化。
2. **[ ] 下线机端 AutoIt 里 `cut_machine` 与 `cut_task` 的映射逻辑**：CutDevice.au3 当前实现是读任务 → 执行 → 回写，`cut_rule` / `cut_device_state` 两张表的写入条件和触发时机需要确认（是执行前 heartbeat、还是执行后 finish 写？还是两者都写？）。
3. **[ ] 手机端增量同步 `since` 参数的边界**：如果服务器把记录删掉了（delete），`GET /api/sync/products?since=...` 不会通知手机端这条记录已删除。需评估是否在同步接口里加"软删除"或"删除列表"。
4. **[ ] OCR 识别速度**：当前每次识别都要启动 PaddleOCR-json.exe（加载模型约 3-4 秒）。是否需要改成启动时常驻？（影响：exe 常驻内存约 300MB；好处：后续识别秒级）。
5. **[ ] 版本检查弹窗的触发时机**：当前是手动点「检查更新」按钮，没做启动时自动检查。PC 端 `updatedialog.cpp` 已写好，缺主界面启动流程里的触发逻辑。
6. **[x] 数据表 `cut_print_log` 的写入完整性**：设备端 `Api.au3` 已使用 `op=log_print`，字段 `taskId/machineId/operator/labelNo/copies/result` 与服务端 `cutLogPrint()` 和 `cut_print_log` 表字段一致；保留本条作为回归检查项。
7. **[x] 下线机 AutoIt 更新覆盖的批处理 `timeout` 命令**：b28 已改为 `ping 127.0.0.1 -n 2 >nul`，兼容 WinXP；同时修复批包里 `!VAR!` 未开启延迟展开导致替换失败的问题，改用 `%VAR%`，并按 `@AutoItExe` 实际文件名替换。
8. **[ ] 设备端主界面列宽二次调整**：用户要求加宽序号/批次/原表序号/回路/截面/颜色/数量/左右端子/状态/操作员，并把「左压前」「右压前」改为当前宽度的 1/3。当前 `_CreateTaskList()` 仍用权重 `[30,180,80,40,60,70,40,40,60,220,220,90,90,30,30,70,80]`，尚未执行。

---

## 7. 关键文件索引

| 模块 | 路径 |
|---|---|
| 构建 | `ND_DrawingManagement_PC/CMakeLists.txt` |
| 主窗口（页面栈、导航、HTTP 服务启动） | `ND_DrawingManagement_PC/ui/mainwindow.cpp/h` |
| 录入页（OCR、粘贴、保存） | `ND_DrawingManagement_PC/ui/entrydialog.cpp/h` |
| 管理页 | `ND_DrawingManagement_PC/ui/managepage.cpp/h` |
| 设置页 | `ND_DrawingManagement_PC/ui/settingsdialog.cpp/h` |
| 工具页（含每日数量录入提醒、每日更新通知的设置项） | `ND_DrawingManagement_PC/ui/toolspage.cpp/h` |
| 右下角通知窗（数据录入通知 / 更新通知共用） | `ND_DrawingManagement_PC/ui/dataentrynotify.cpp/h` |
| 模具端子录入 | `ND_DrawingManagement_PC/ui/moldterminalpage.cpp/h` |
| 模具端子查询 | `ND_DrawingManagement_PC/ui/moldquerypage.cpp/h` |
| 下线任务 | `ND_DrawingManagement_PC/ui/cuttaskpage.cpp/h` |
| 数据库（表定义 + CRUD + 迁移） | `ND_DrawingManagement_PC/db/databasemanager.cpp/h` |
| HTTP 服务器（所有接口路由） | `ND_DrawingManagement_PC/server/lanhttpserver.cpp/h` |
| 统一提示窗 | `ND_DrawingManagement_PC/ui/ndmessagebox.cpp/h` |
| 登录遮罩 | `ND_DrawingManagement_PC/ui/overlaymask.cpp/h` |
| OCR 图片拖入框 | `ND_DrawingManagement_PC/ui/ocrimagedrop.cpp/h` |
| 下线机 AutoIt 主程序 | `ND_DrawingManagement_Device/CutDevice.au3` |
| 下线机 HTTP 接口封装 | `ND_DrawingManagement_Device/Api.au3` |
| 下线机配置 | `ND_DrawingManagement_Device/config.ini` |
| 手机端主 Activity / 同步逻辑 | `ND_DrawingManagement_Android/app/src/main/java/.../MainActivity.kt` |

---

## 8. 目录结构速览

```
ND_DrawingManagement/
├── ND_DrawingManagement_PC/         # Qt 桌面主程序（权威数据源）
│   ├── CMakeLists.txt
│   ├── main.cpp
│   ├── db/
│   │   └── databasemanager.cpp       # 20 张表定义 + 迁移 + CRUD
│   ├── server/
│   │   └── lanhttpserver.cpp         # 所有 HTTP 路由
│   ├── ui/                           # 全部页面/窗口
│   ├── model/                        # Product / Drawing / FieldConfig / 等
│   ├── resources/                    # 图标 QRC（logo.ico 等）
│   ├── data/                         # 运行时数据库目录
│   └── build/                        # 本工程的构建输出（独立）
│
├── ND_DrawingManagement_Android/     # Kotlin 手机端
│   └── app/src/main/...
│
├── ND_DrawingManagement_Device/      # AutoIt 下线机
│   ├── CutDevice.au3                 # 主程序（UI + 任务循环）
│   ├── Api.au3                       # HTTP 接口封装
│   ├── Config.au3                    # config.ini 读写
│   ├── Print.au3                     # DL-720C 标签打印
│   ├── Http.au3 / Json.au3           # 基础库
│   └── config.ini                    # 运行时配置（server / machineNo / deviceKey ...）
│
├── ocr/                              # 离线 OCR 引擎（运行时放置）
│   └── PaddleOCR-json/
│
├── docs/
│   └── PLATFORM.md                   # ← 就是本文件，后续 AI 所有改动都先读这里
│
└── build/                            # 旧版残留，忽略
```

---

## 9. 全平台打包编译规范 & 版本号规则

### 9.1 版本号统一格式

| 端 | 格式 | 示例 | 设置位置 |
|---|---|---|---|
| PC 端 | 语义化 `major.minor.patch` | `1.0.0` | `app_meta` 表 `version` 字段 |
| 手机 APK | `yyyy-MM-dd`（文件名或修改时间） | `2026-11-09` | APK 文件名 `ND_DrawingManagement_v1.0_2026-11-09.apk` |
| 下线机 Device | `yyyyMMdd-bXX-x86` / `yyyyMMdd-bXX-x64` | `20261119-b36-x86`、`20261119-b36-x64` | `CutDevice.au3`：`$gAppVerBase = "20261119-b36"` + `$gAppArch = "x86"/"x64"` |

**版本号更新时间**：每次有改动要打包发布时，按当天日期更新。同一天多次构建用递增的 `-b` 后缀区分。

**设备端安装包文件名（b32 起）**：`下线机设备<yyyyMMdd-bXX>-x86.exe` / `下线机设备<yyyyMMdd-bXX>-x64.exe`。由 `build_device.ps1` 生成；PC 本地安装、客户端上传、服务端接收统一走 `UpdateManager::isDevicePackageName()`（以 `-x86.exe` / `-x64.exe` 结尾，同时兼容旧 `CutDevice_*_x86.exe`）。版本号直接从文件名正则解析，必须带位数后缀，避免设备端把不同架构当成同一版本。`build_device.ps1` 必须保存为 **UTF-8 with BOM**，否则 Windows PowerShell 5.1 会把中文名读成 ANSI 乱码。

### 9.2 打包命令

**PC 端（Qt / CMake + Ninja）**：
```powershell
cd ND_DrawingManagement_PC\build
cmake --build . --config Release
# 输出：bin\ND_DrawingManagement_PC.exe + 所有 DLL
# 打包成安装包：用 Qt Installer Framework 或手动 Copy-Item bin\* → 安装包目录
```

**下线机 Device（AutoIt 3，32/64 位一起出包）**：
```powershell
# 一条命令完成：Au3Check + 分别调用 Aut2exe.exe(x86) / Aut2exe_x64.exe(x64)
# 统一图标：ND_DrawingManagement_PC/logo.ico；禁用 UPX 用 /nopack（不是 /packexe）
powershell -ExecutionPolicy Bypass -File `
  "E:\HouYawei\01.QTProject\ND_DrawingManagement\ND_DrawingManagement_Device\build_device.ps1"
# 输出：CutDevice_yyyyMMdd-bXX-x86.exe / CutDevice_yyyyMMdd-bXX-x64.exe
```

**手机端（Android Studio / Gradle）**：
```bash
cd ND_DrawingManagement_Android
./gradlew assembleDebug   # 或 assembleRelease
# 输出：app/build/outputs/apk/debug/ND_DrawingManagement.apk
```

### 9.3 版本发布流程（三端统一）

1. 修改代码 → 更新版本号（按 9.1 格式）
2. 本地编译打包 → 测试
3. PC 端启动 HTTP 服务器（`ND_DrawingManagement_PC.exe`）
4. PC 设置页 → 版本管理 Tab → 选择对应安装包 → **上传**（下线机需分别上传 32 位、64 位）
5. 安装包会被存到服务器工作路径 `安装包/` 目录；PC/手机各一份，下线机按位数各一份，旧版自动替换
6. 客户端（手机/下线机）在同一局域网内点「检查更新」即可下载覆盖；下线机按自身位数取对应包

### 9.4 AutoIt 端已知限制

| 问题 | 原因 | 解决方案 |
|---|---|---|
| `Au3Check: For Each Next` 语法错 | AutoIt 数组不支持 For Each | 用 `For $i = 0 To UBound($arr) - 1` |
| `Aut2exe` 静默失败不生成 exe | `/packexe` 是无效参数；32/64 位需要不同编译器 | 用 `/nopack` 禁用 UPX；x86 用 `Aut2exe.exe`，x64 用 `Aut2exe_x64.exe`（见 `build_device.ps1`） |
| 运行时 `Variable must be of type Object` | .NET COM（如 DataGridView）在 AutoIt STA 下属性赋值失败 | 放弃 .NET COM，改用原生控件 |
| `timeout.exe` 不存在 | Win7 及更早系统无此命令 | 用 `ping 127.0.0.1 -n 2 >nul` 替代（b19 修复） |
| ListView 单元格不换行 / 表头截断 | 原生 ListView 单元格不支持真正换行，列多时省略号无法彻底消除 | 当前用 1×36 ImageList 模拟行高 + 底部完整详情区；仍需按用户要求二次调列宽（见 TODO 8） |
| 点击行不刷新详情 | Tab 嵌套控件 WM_NOTIFY 收不到 | 用 `AdLibRegister("_PollSelection", 50)` 轮询 |
| `StartDocPrinterW` 返回 0 且 `GetLastError=124` | 第 2 个参数是 **Level（1/2）**，不是结构字节数 | 传 `1`（DOC_INFO_1）或 `2`（DOC_INFO_2），见 9.5 |

### 9.5 下线机标签打印（Print.au3）

**打印机**：Deli DL-720C（TSC/TSPL 指令集，203dpi → 8 dots/mm），USB001 本地端口，**未共享**。

**打印路径（b24 起）**：

1. `OpenPrinterW` —— 句柄必须用 `DllStructCreate("ptr")` 接（AutoIt 的 `ptr*` ByRef 无效）
2. `StartDocPrinterW(h, 1, DOC_INFO_1)` —— **Level 传 1**；结构为 `ptr;ptr;ptr`（pDocName / pOutputFile=NULL / pDatatype="RAW"）。代码依次尝试 `Level1+RAW → Level1+NULL → Level2+RAW`，任一成功即进入 WritePrinter
3. `StartPagePrinter` → `WritePrinter` → `EndPagePrinter` → `EndDocPrinter` → `ClosePrinter`
4. 任一步失败 → fallback：写 `%TEMP%\cut_rawprint.ps1`（C# P/Invoke，同样 Level=1），用 `powershell -File` 执行，**输出写日志**（`OK 1092` 表示成功写入 1092 字节）

> 注意：`cmd print /d:\\localhost\打印机名` 要求打印机已共享，本地 USB 打印机不可用；`copy /b file.prn USB001:` 在 Windows 上不支持（返回码 1）。

**标签版式（b27 起）**：`SIZE 40mm × 100mm`，12 行表格 + 底部大空框，与 Excel `标签40_100` 表一致。
**空行中心必须落在 y=400（纸张正中折叠线）**——纸张对折后空白区绕线贴合。

```
┌─────────────────────────┐  ← 外框 BOX 8,8,312,792,2
│ 型号：  ND-HY-FT-F008…   │  行0  8..53   （上半区行高 45）
│ 序号 │ 68 │ 机台号 │ 0   │  行1  53..98
│ 材质 │ …  │ 数量   │ …   │  行2  98..143
│ 截面积│ …  │ 颜色   │ …   │  行3  143..188
│ 线号 │ …  │ 长度   │ …   │  行4  188..233
│ 左剥皮│ …  │ 右剥皮 │ …   │  行5  233..278
│ 左端子 │ …（跨列）        │  行6  278..323
│ 右端子 │ …（跨列）        │  行7  323..368
├─────────────────────────┤
│        （空行）           │  行8  368..432，**中心 400 = 纸张正中**
├─────────────────────────┤
│ 左压前 │ …（跨列）        │  行9  432..484（下半区行高 52）
│ 右压前 │ …（跨列）        │  行10 484..536
│ 批次号：│ …（跨列）        │  行11 536..588
├─────────────────────────┤
│      （底部大空框）        │  588..792（约 25mm 高）
└─────────────────────────┘
```

- 行位置由 `$aTop[12]` / `$aH[12]` 两张表驱动（不再用统一行高），改高度只需改这两张表
- 上半区 8 行 × 45 = 360（8→368）；空行 64（368→432，中心 400）；下半区 3 行 × 52（432→588）
- 行 8 为**空行**（折叠区），只有横线、不画竖线、无文字
- 底部大空框由外框延伸到 `y=792`，内部无分隔线
- 列边界：`L=8, A=88, B=176, C=256, R=312`（A/B/C/D 列宽 80/88/80/56）
- `_TsplCell()` 自动按宽度选字体：`TSS24.BF2 → TSS16.BF2 → TSS12.BF2`，仍超宽则截断尾部
- 估算宽度：汉字 = 字号，ASCII = 字号/2（`_TextWidthPx`）
- 长标签（"左压前作业"）拆两行居中（`_TsplCellTwoLine`）

---

## 10. 变更记录（Changelog）

> 对话里每轮改动都要追加到这里，按版本倒序。版本号规则见 9.1。

### 10.1 下线机设备端（ND_DrawingManagement_Device/）

| 版本 | 改动 |
|---|---|
| **b36** | **修正数量张次显示**：标签「数量」格由 `1./2` 改为 `1/2`（`_TsplCell(..., $iCurrent & '/' & $iTotal)`），去掉多余的小数点 |
| **b35** | **数量显示与整条完成**：详情/浮窗「数量」只显示总数（`2/2` → `2`）；「开始打印」一次按数量自动出 N 张（`1/N`…`N/N`）；点「已完成」改为**整条任务一次完成**——`_ApiConfirm()` 新增 `qty` 参数传剩余数量，服务端 `confirm_task` 支持 `qty`（`nextDone=min(total,done+qty)`），成功后 `_MoveNextPending()` 自动跳到下一条任务，不再按数量分批确认 |
| **b34** | **按数量分张打印**：`_PrintTaskLabel()` 改为 `For $i = 1 To 数量` 逐张打印，每张内容里「数量」显示 `i/N`（数量=2 → 第一张 1/2、第二张 2/2），每张 `PRINT 1,1` 只出一份；`_BuildLabel()` 新增第 4 参数 `$iCopies`（-1 = 用配置 `$gCopies`）；每张打印成功单独上报 `cut_print_log`（`label_no=i/N`, `copies=1`）并写日志「第 i/N 张打印完成」 |
| **b33** | **服务器通讯失败友好提示**：注册 `ObjEvent("AutoIt.Error", "_ComErrorHandler")`，WinHttp 连不上服务器时不再弹 AutoIt 原生「The requested action with this object has failed」；`Http.au3` 每个 COM 调用（Open/Send/ResponseText/Stream）补 `@error` 检查并返回明确错误码；新增 `_ShowServerError()`，启动刷新、手动刷新、检查更新失败都弹中文提示（含服务器地址与排查项） |
| **b32** | **中文包名与派单「其他」**：AutoIt 改出 `下线机设备20261119-b32-x86/x64.exe`；PC 端本地安装/客户端上传/服务端接收统一 `isDevicePackageName()` 校验（含 `-`/`_` 两种后缀），清理旧包用多 glob；设置页过滤与提示同步。配置与 log 迁到 `D:\ND_DrawingManagement_Device`（D 盘不可用时回退程序目录，旧 config.ini 自动迁移）；设备端更新批处理改按 PID 等待自身退出、去掉 `chcp 65001` 避免中文路径乱码。派单规则目标机台新增「其他」（人工/手动），端子/防水栓/材质含「屏蔽」自动归「其他」 |
| **b31** | **彻底修复白底**：删除原生 `GUICtrlCreateTab`，改为三个自绘按钮 + 单个 ListView（任务列表按 `$gCurTab` 重填），避免 Tab 客户区强制白底；任务号权重 150→110，型号 90→140；生成 b31 x86/x64 两个安装包 |
| **b30** | **设备端术语与表格**：主界面「批次」改为「型号」并新增「批次号」列，移除左端子/右端子列（仍在详情区显示）；原生白色表头改为自绘深色 Label，Tab 背景统一深色；浮窗「批次:」改为「型号:」并新增「批次号:」；底部详情同步改为型号/批次号 |
| **b29** | **界面精简**：浮窗底部按钮由「托盘」改为「最小化」；状态筛选从下拉改为 4 个按钮（全部/待生产/进行中/已完成），去掉设备端用不到的「待派工」「已作废」；右下版本号字号放大并提亮；设置页设备密钥改为 `ND-M1`~`ND-M5` 下拉，隐藏测试打印用的「份数」并固定为 1，移除「系统已安装打印机」列表（打印机下拉已包含） |
| **b28** | **32/64 位版本与检查更新**：`$gAppVerBase="20261119-b28"` + `$gAppArch`，标题显示完整 `yyyyMMdd-bXX-x86/x64`；版本号/检查更新移到主界面右下角（设备设置左边）；恢复状态下拉筛选（全部/待生产/进行中/已完成/已作废）并作用于三个 Tab 列表；`/api/update/device/info?arch=` 与 `/package?arch=` 按位数取包；更新批处理改 `ping` 延时、修复 `!VAR!` 延迟展开和 exe 文件名替换 |
| **b27** | **空行居中对齐折叠线**：改为 `$aTop/$aH` 行位置表驱动——上半区 8 行 × 45（8→368），空行 64（368→432，**中心 400 = 纸张正中**），下半区 3 行 × 52（432→588），底部大框 588→792。解决"空行偏下、折叠后绕线贴不齐" |
| **b26** | 标签表格：右端子与左压前作业之间加**空行**；批次号下方加**底部大空框**；行高统一 52 |
| **b25** | 标签版式重写：自由排版 → **带边框表格**（`BOX` 外框 + `BAR` 分隔线），严格对齐 Excel `标签40_100` 第二张表；`_TsplCell` 自动降字号（24/16/12）居中；长标签拆两行 |
| **b24** | **打印终于成功**：`StartDocPrinterW` 第 2 参数是 **Level**（传 1），此前误传结构大小 20 → `GetLastError=124`；fallback 改为生成 `%TEMP%\cut_rawprint.ps1`（C# P/Invoke）执行，规避引号嵌套崩溃 |
| **b23** | 删除 PowerShell 引号嵌套（曾导致弹窗崩溃）；fallback 简化。**失败** |
| **b22** | 尝试 8 种 datatype×dwMode 组合 + 三层 fallback。**失败** |
| **b21** | `StartDocPrinterW` 加 SetLastError 诊断；引入 PowerShell fallback。**失败** |
| **b20** | `OpenPrinterW` 句柄必须用 `DllStructCreate("ptr")` 接收（AutoIt `ptr*` ByRef 恒为 0）；打印机输入框改 **ComboBox**，打开设置页自动 WMI 枚举；新增「刷新列表」 |
| **b19** | 设备设置页新增**打印机信息区**（配置名 / 默认打印机 / 已装打印机列表 / 测试打印按钮）；`Print.au3` 重写（DOCINFOW 补全、默认打印机回退、全流程日志 `logs/client-YYYYMMDD.log`） |
| **b18** | 主界面显示版本号 + 「检查更新」按钮（`_DoCheckUpdate`：查 `/api/update/device/info` → 下载 → cmd 批处理覆盖重启）；`Http.au3` 新增 `_HttpGet` / `_HttpGetBinary` |
| **b17** | **修点击行不刷新详情**：`_GUICtrlListView_GetSelectedIndices` 第 2 参数从 `False` 改 **`True`**（False 返回字符串，`IsArray` 永远为假 → 直接 return） |
| **b16** | 改用 `AdLibRegister("_PollSelection", 50)` 轮询当前 Tab 的 ListView 选中项 |
| **b15** | 尝试 `GUIOnEventMode=1` + 主循环轮询。**失败**（破坏阻塞式事件） |
| **b14** | 列宽按权重 `[30,180,80,40,60,70,40,40,60,220,220,90,90,30,30,70,80]` 等比放大填满窗口；尝试 `WM_NOTIFY` 刷新详情。**失败** |
| **b13** | 放弃 .NET DataGridView（COM 报 `Variable must be of type Object`），回归原生 ListView；17 列重排使总和 ≤ 可视宽度（无横向滚动）；字体 11 加粗 + 行高加大；列表文字改**纯白** |

### 10.2 PC 端（ND_DrawingManagement_PC/）

| 模块 / 文件 | 改动 |
|---|---|
| `db/databasemanager.cpp` | **派单修正**：① `cutComputeAssignments()` 的「材料组跟随」不再把「其他（人工/手动下线）」当作可跟随机台——同材料里只要有一条屏蔽件/人工件，之前会把整组正常线径明细一起拽到人工线；现改为仅跟随真实自动机台，`target=0` 或「其他」都重新按线径规则选机台，只有**无任何线径规则覆盖**时才归「其他」；② `cutSaveRule()` 新增去重校验：同 `field + match_type + match_value`（忽略大小写/首尾空格）+ 同 `target_machine` 视为重复，拒绝保存并提示「已存在相同规则 #id」 |
| `ui/cuttaskpage.cpp` | **任务与派工修复**：① 状态筛选改为 全部/待生产/进行中/已完成（映射 `all/assigned/doing/done` → `status=1/2/3`）；② 修复双击「机台 / 状态」失效——委托此前错挂在第 10/13 列（右压前/完成），改为第 **11/14** 列；③ `fillRow()` 取状态误用 `rowData.at(15)`（操作员），改为 `at(14)`，并同步可编辑标志到第 14 列 |
| `ui/toolspage.cpp` / `ui/toolspage.h` / `util/autostart.h` | 关于页「运行设置」新增**更新通知**：勾选框 + 每日检测时间 `QTimeEdit`（默认 09:00），配置键 `notify/updateEnabled`、`notify/updateTime`、`notify/updateLastStamp` |
| `ui/mainwindow.cpp` / `ui/mainwindow.h` | 新增 `checkUpdateNotify()`（每分钟检查，刚跨过设定时刻才执行，按「日期+时刻」去重）与 `showUpdateNotify()`：查到服务器新版本时右下角弹「更新通知 · 服务器有新版本 vX，是否更新？」（确定/取消），确定后打开现成的 `UpdateDialog::checkForUpdate()` 下载安装；离线查询失败静默跳过 |
| [cuttaskpage.cpp](file:///e:/HouYawei/01.QTProject/ND_DrawingManagement/ND_DrawingManagement_PC/ui/cuttaskpage.cpp) | 线束页 4 张表（批次/预览/任务/派单规则）文字与表头统一 **`#FFFFFF` 纯白**（QSS + `centeredItem()` 的 `setForeground`） |
| [cuttaskpage.cpp](file:///e:/HouYawei/01.QTProject/ND_DrawingManagement/ND_DrawingManagement_PC/ui/cuttaskpage.cpp) | 任务与派工表：固定列宽总和 1275→1146，`minimumSectionSize` 100→60，Stretch 列 2→3（机台号 + 左/右压前作业），加 `setStretchLastSection(true)` → **非全屏也显示全** |
| [settingsdialog.cpp](file:///e:/HouYawei/01.QTProject/ND_DrawingManagement/ND_DrawingManagement_PC/ui/settingsdialog.cpp) | 版本管理页新增**下线机 exe 上传**区块（选择/上传按钮，默认路径 `…\ND_DrawingManagement_Device`） |
| [updatemanager.cpp](file:///e:/HouYawei/01.QTProject/ND_DrawingManagement/ND_DrawingManagement_PC/server/updatemanager.cpp) | `PackageKind` 加 `Device`；版本号从 AutoIt 文件名提取日期；`checkUpdate` 支持 device |
| [lanhttpserver.cpp](file:///e:/HouYawei/01.QTProject/ND_DrawingManagement/ND_DrawingManagement_PC/server/lanhttpserver.cpp) | 新增免登录路由 `/api/update/device/info`、`/api/update/device/package` |
| `ui/mainwindow.cpp` / `ui/toolspage.cpp` / `ui/settingsdialog.cpp` | 补全 `#darkMenuTabs` QSS（TabBar/Tab/StackedWidget 统一深色）并开启 `WA_StyledBackground`，修复工具页/设置页二级菜单右侧背景；已通过 CMake/Ninja 编译验证 |
| `ND_DrawingManagement_Device/Api.au3` | 外部 AI 续改后写操作 op 统一为 `start_task` / `confirm_task` / `log_print`，`taskNo` / `batchNo` 为空时回退到 `id` / `batchName` |
| `ui/settingsdialog.cpp` / `ui/settingsdialog.h` | 版本管理页拆成 PC / 手机 APK / 下线机 32 位 / 下线机 64 位四个上传区；分别查询和显示两种位数的服务器版本（PC 本体仍只构建 64 位） |
| `server/updatemanager.*` / `server/lanhttpserver.*` | 新增 `PackageArch`；Device 元数据拆为 `update_device_x86.json` / `update_device_x64.json`，上传/查询/下载按位数隔离；PC 包 glob 收窄为 `ND_DrawingManagement_PC*.exe`，避免清理设备端 exe |
| `ND_DrawingManagement_Device/build_device.ps1` | 统一读取版本、统一使用 `logo.ico`，分别调用 `Aut2exe.exe` / `Aut2exe_x64.exe` 生成 x86/x64 两个安装包（`/nopack` 禁用 UPX） |
| `util/xlsreader.cpp` | **只解析 Workbook 流中的第一张 worksheet**（遇到第二张 `BOF dt=0x0010` 或第一张 `EOF` 即停），修复隐藏/模板 sheet 覆盖第一张表导致「批次号：样品」被读成 `006`；同时修正 SST 跨 CONTINUE 时标志字节未跳过的问题 |
| `ui/cuttaskpage.cpp` | 标题区按「型号：」「批次号：」标签 + 同行右侧最近非空单元格解析；导入批次名称固定取表格型号（去掉改名弹窗）；全页术语统一：已导入线束 / 共 N 套线束 / 删除线束，表头为 线束ID、型号、批次号、来源文件、明细数、导入人、导入时间；任务表第三列「型号」+ 第四列「批次号」；预览机台分布按机台 ID 排序、其他最后 |
| `db/databasemanager.cpp` | 物理表 `cut_batch` 改名 `cut_harness` 并保留旧库自动迁移；删除最后一套线束后重置 `cut_harness/cut_item/cut_task/cut_print_log` 的 `sqlite_sequence`，新导入 ID 从 1 开始 |
| `server/updatemanager.*` / `server/lanhttpserver.cpp` / `ui/settingsdialog.cpp` / `build_device.ps1` | b32 统一设备端包名为 `下线机设备yyyyMMdd-bXX-x86/x64.exe`：三处校验合一为 `isDevicePackageName()`，清理旧包走多 glob，设置页过滤/提示同步；版本号从文件名解析（含位数后缀） |
| `util/cuttypes.h` / `db/databasemanager.cpp` / `ui/cuttaskpage.cpp` | 派单新增「其他」机台：规则下拉可选 OTHER，引擎允许硬规则指向它；`cutItemHasShield()` 把端子/防水栓/材质含「屏蔽」的明细在第一遍直接归「其他」（当时的 Excel 机台列识别逻辑已于后续删除，见本表末行） |
| `ui/mainwindow.cpp` | 补 `#btnDanger:disabled` QSS：未选中派单规则时「禁用规则 / 删除规则」与「保存修改」同步显示为灰（此前只改了 enabled 状态，红色按钮视觉上仍像可点） |
| `db/databasemanager.cpp` | **修复「删除线束后重开又出现」**：`cut_batch` 迁移时 `oldTable` 结果集未关闭，SQLite 锁住旧表使 `DROP TABLE cut_batch` 一直静默失败，旧表残留 → 每次启动 `INSERT INTO cut_harness ... FROM cut_batch` 把已删线束回填。改为 `probe.finish()` 先释放读锁、用 `app_meta(key='cut_harness_migrated')` 保证只迁移一次、`DROP TABLE IF EXISTS` 最多重试 3 次并记日志、顺带清 `sqlite_sequence` 里的 `cut_batch` |
| `db/databasemanager.cpp` / `ui/cuttaskpage.cpp` | 删除线束加双保险：`cutDeleteBatch` 对 `batchId<=0` 直接报错、用 `numRowsAffected()` 确认主记录真的删掉，否则回滚报错；UI 侧 UserRole 取不到时退回第 0 列文本，删除后回查 `cutBatches()` 确认该 ID 已消失才提示「删除完成」 |
| `db/databasemanager.cpp` / `ui/cuttaskpage.cpp` | **智能派工不再使用下线表 Excel 的「机台」列**（删除原第三遍 `resolveMachine()` 硬分配）：该列只是随表带过来的历史填写，与派单规则并不一致（如 `828904-1` 在 Excel 里写 1，但没有任何规则要求它上一号机），当硬约束会让单台机台任务量暴涨。现在机台只由「防水栓/端子硬规则 → 线径能力候选 → 同材料归组 → 挑当前最闲」决定。实测同批次分布由 `57/28/27/31/27/其他14` 变为 `35/34/34/34/33/其他14`；`281934-2` 等仍由规则锁定到四号机，结果不受影响。按钮提示与派工完成弹窗文案同步改为「规则锁定 N 条」 |
| `ui/cuttaskpage.cpp` | **下线表导入修正**：① 序号改为读 Excel「序号」列原值（先在前 40 行按表头文字「序号」定位列），不再按检测到的数据行自动计数——自动计数中途多算/漏算一行就会整体错位（实测 Excel 184 行是 K06，导入后显示成 K07）；只有在找不到序号列或该格不是数字时才回退为自动计数。② 原表末尾的「机台」列不再读取，导入预览表同步去掉「机台」列（该字段随后已从表中删除，见下一行）。已导入的旧批次仍带旧序号，需删除线束后重新导入 |
| `db/databasemanager.cpp` / `util/cuttypes.h` | **彻底删除 `cut_item.machine_no` 字段**：`CREATE TABLE cut_item` 不再建该列，`cutItems()` / `cutImportBatch()` 的 SQL 与 `CutItem` 结构体/JSON 序列化同步移除；`createTables()` 启动时用 `PRAGMA table_info` 检测老库，存在该列则执行 `ALTER TABLE cut_item DROP COLUMN machine_no`（SQLite ≥3.35 支持，已在 3.37.2 副本上验证，184 行数据不受影响）。派工机台只看 `cut_task.machine_id` |
| `ui/cuttaskpage.cpp` | **下线表被手工改动时的容错**：数据行不再用「长度必须是数字」做唯一准入条件，改为「回路编号 + 左端子非空，且长度或截面积至少一个是数字」。长度写成「取消」「作废」或留空时**用 `0` 占位并保留该行**，避免丢一行导致后续「原表序号 → 明细」整体错位；表尾「不下料 / 卡给压接」汇总行因长度、截面积都空，仍会跳过。解析完成后弹「原表数据异常」提示，逐条列出「原表序号 X（回路 Y）：长度「取消」不是数字，已用 0 替代」，导入信息栏同时显示异常处数。已在实测文件上验证：245 条明细、仅 1 处异常（序号 126 长度=取消），184→K06、185→K07 与原表一致 |

### 10.3 经验沉淀（踩过的坑）

| 现象 | 真因 | 正确做法 |
|---|---|---|
| `OpenPrinterW` 返回 1 但 hPrinter 恒为 0 | AutoIt `DllCall("ptr*", $var)` 的 ByRef 输出无效 | `DllStructCreate("ptr")` + `DllStructGetPtr` 传参，再 `DllStructGetData` 读回 |
| `StartDocPrinterW` 失败、`GetLastError=124` | 第 2 参数是 **Level**，不是 `DllStructGetSize()` | Level=1 + DOC_INFO_1（3 指针） |
| `EnumPrintersW` 枚举 0 台 / 崩溃 | DllCall 参数栈错位、空指针解引用 | 改用 **WMI** `Win32_Printer` 枚举（AutoIt `ObjGet("winmgmts:...")`） |
| `copy /b x.prn USB001:` 返回 1 | Windows 不支持直接写 USB 端口 | 用 Win32 WritePrinter 或 .NET P/Invoke |
| PowerShell 命令崩溃 | AutoIt 双引号里嵌 PowerShell 引号 | 把脚本**写成 .ps1 文件**再 `powershell -File` 执行 |
| `GetSelectedIndices($lv, False)` 判断不了选中 | 返回字符串而非数组 | 第 2 参数传 `True` |
| 下线表「批次号：样品」被读成 `006` | `XlsReader` 扫了整个 Workbook 流，后面的隐藏/模板 sheet 单元格覆盖了第一张表 | 只解析第一张 worksheet：首个 `BOF dt=0x0010` 开始，遇到下一个 `BOF` 或 `EOF` 结束 |
| 设备端表格顶部仍是白底 | 原生 `GUICtrlCreateTab` 客户区强制白色，`SetBkColor` 和自绘表头都盖不住 | 删除原生 Tab，改为 3 个按钮 + 单个 ListView，按 `$gCurTab` 重填数据 |
| 点空白处后「禁用规则/删除规则」仍显示红色，看着像可点 | `setEnabled(false)` 已生效，但 QSS 只定义了 `#btnDanger` / `:hover` / `:pressed`，没有 `:disabled`，显式背景色覆盖了 Qt 默认灰化 | 补 `#btnDanger:disabled{background:rgba(...);color:...}`，和已有的 `btnEntry:disabled` 对齐 |
| 服务器连不上时弹 AutoIt 原生错误框「The requested action with this object has failed」 | WinHttp COM 调用失败未接管，`ObjCreate/Open/Send/ResponseText` 任一处抛异常都会触发默认 COM 错误对话框并终止脚本 | 启动注册 `ObjEvent("AutoIt.Error", "_ComErrorHandler")`（处理器只记日志、不读异常对象属性，避免递归）；每个 COM 调用后查 `@error` 并返回 False；调用方用 `_ShowServerError()` 弹中文提示 |
| 删除线束提示成功，重开软件又出现（ID 还是原值） | `cut_batch`→`cut_harness` 迁移里 `oldTable` 的查询结果集没关闭，SQLite 持有旧表读锁，`DROP TABLE cut_batch` 静默失败；旧表留着，每次启动都把它回填进 `cut_harness` | 迁移前 `finish()` 关游标；用 `app_meta` 标记只迁移一次；`DROP TABLE IF EXISTS` 重试并检查 `lastError()`；删除接口校验 `batchId>0` 且 `numRowsAffected()>0`，UI 删除后回查确认 |

---

## 11. 2026-11-19 源码审计补充（外部 AI 续改现状）

> 用户反馈「昨天又用其他 AI 工具改了下线机设备端和打印机驱动」。以下为截至 2026-11-19 对源码的实际审计结论；与前文冲突时以本节为准。

### 11.1 版本与配置

- 当前设备端版本：基准 `Global $gAppVerBase = "20261119-b33"`；运行时按编译位数拼成 `20261119-b33-x86` / `20261119-b33-x64`。安装包文件名改为中文 `下线机设备20261119-b33-x86.exe` / `-x64.exe`；`@AutoItExe` 的文件名即完整版本号，用于更新时替换自身。
- 打包脚本 `build_device.ps1` 统一读 `$gAppVerBase`，用 `ND_DrawingManagement_PC/logo.ico` 作图标，分别调用 `Aut2exe.exe`（x86）和 `Aut2exe_x64.exe`（x64），`/nopack` 禁用 UPX；已验证生成 exe 的 PE 架构分别为 `0x014C` / `0x8664`。脚本含中文输出名，**必须以 UTF-8 with BOM 保存**。
- b30 重新出包时 x64 旧文件正被设备端进程占用，因此额外生成 `CutDevice_20261119-b30-x64-r2.exe`（同为 `0x8664`）；关闭正在运行的 x64 程序后可用该 r2 文件替换。
- **b32 起配置与日志路径**：统一放 `D:\ND_DrawingManagement_Device\config.ini` 与 `D:\ND_DrawingManagement_Device\logs\`。D 盘不存在或不可写时回退到 `@ScriptDir\ND_DrawingManagement_Device`；首次运行若新位置没有 config.ini 且旧 `@ScriptDir\config.ini` 存在，会自动复制迁移。`_LogLine()` 写日志前也确保目录存在。
- `config.ini` 现场值：server=`http://192.168.1.16:8080`、machineNo=`2`、deviceKey=`ND-M2`、printer=`Deli DL-720C`、paper=40×100mm、gap=2、font=`TSS24.BF2`、password=`0000`、refreshOnStart=1、operator=侯某某。
- `Config.au3` 默认值：server=`http://192.168.1.16:8080`、machineNo=`1`、deviceKey=`ND-M1`、printer=`DL-720C`、copies=1、paper=40×100、gap=2、font=`TSS24.BF2`、password=`0000`、`ui.refreshOnStart=1`；机台名由 `_MachineName()` 生成，配置里的 `machineName` 已不再作为设置项使用。
- 机台号限制 1~5；1~5 对应 一号机~五号机；操作员为空时回退到机台名。

### 11.2 设备端 UI 现状

- 浮窗：`FLOAT_W=430` / `FLOAT_H=560`，`WinMove` 到 `@DesktopWidth-438`、`@DesktopHeight-602`，即桌面右下角；`WS_POPUP + WS_EX_TOPMOST + WS_EX_TOOLWINDOW`。
- 浮窗信息按员工阅读顺序拆成独立 Label：当前机器 / 任务号 / 原表序号 / 型号（大字） / 型号 / 批次号 / 操作员 / 回路 / 截面 / 长度 / 左剥皮 / 右剥皮 / 左压前 / 右压前 / 数量 / 状态；b30 起「批次」统一改为「型号」并新增「批次号」；按钮：上一条、下一条、开始/打印（绿）、已完成（红）、最小化（b29 起，原「托盘」）。
- 主窗口：随桌面自适应（最小 1000×620），b31 起 3 个自绘标签按钮（全部/未完成/已完成）+ 单个 `$lvTasks`，按 `$gCurTab` 重填对应映射；16 列表头：`序号|任务号|型号|批次号|原表序号|回路|材质|截面|颜色|长度|左压前|右压前|数量|完成|状态|操作员`；左端子/右端子只在详情区显示。
- 表格：原生 ListView + `$LVS_NOCOLUMNHEADER`，自绘深色标题 Label 替代白色系统表头；不再使用原生 Tab，主窗口背景即 `0x2B3046`。b31 权重 `[30,110,140,60,40,60,70,40,40,60,150,150,30,30,70,80]`（任务号收窄给型号）；字体 11 加粗、Microsoft YaHei；`_GUIImageList_Create(1,36,...)` 模拟行高；颜色走 `_ColorRef()`（BGR）；列对齐居中。
- 底部详情区 `lblDetail`（`gMainH-244`，高 132）显示选中任务完整字段，b30 起标题改为「型号」并新增「批次号」，左/右端子仍在这里展示；通过 `AdLibRegister("_PollSelection", 50)` 轮询 Tab/ListView 选中项，`_ShowTaskDetail()` 用二维下标直接读 `$gTasks[$r][$T_xxx]`（AutoIt 不支持 `Local $a = $gTasks[$i]` 取整行，b12 已修）。
- 主按钮与浮窗一致：刷新/设为当前任务/打印标签/开始/打印/已完成/返回浮窗/设备设置；**b28 起版本号文字与「检查更新」按钮移到主界面右下角、紧靠「设备设置」左边**，不再挤占表格上方空间。
- **b29 状态筛选按钮组**：Tab 上方改为 4 个深色按钮「全部 / 待生产 / 进行中 / 已完成」，选中项绿色高亮；去掉设备端不会用到的「待派工」「已作废」。`_FillOneList()` 同时应用 Tab 过滤与 `$gStatusFilter`，筛选后三个 Tab 都同步生效。
- **b29 设置页精简**：设备密钥由输入框改为 `ND-M1`~`ND-M5` 下拉；隐藏只在测试打印时用到的「份数」并固定为 1；移除「系统已安装打印机」列表（打印机下拉已列出全部），只保留配置打印机与系统默认打印机简要信息。右下版本号文字放大到 12 号加粗并提亮。

### 11.3 打印驱动与标签版式（Print.au3）

- 打印机：Deli DL-720C，TSPL，40×100mm、203dpi（8 dots/mm）、GAP 2mm、`DIRECTION 1`。
- 标签由 `_BuildLabel()` 生成：12 行表格 + 底部大空框；行位置由 `$aTop[12]` / `$aH[12]` 驱动（上半区 8×45=8..368，空行 64=368..432，中心 400=纸张折叠线，下半区 3×52=432..588，大框 588..792）。
- 列边界 `L=8, A=88, B=176, C=256, R=312`；`_TsplCell()` 按宽度自动降字号 `TSS24.BF2 → TSS16.BF2 → TSS12.BF2`，`_TsplCellTwoLine()` 处理「左压前作业/右压前作业」两行标签。
- RAW 打印主链路：`_ListPrinters()` 用 WMI `Win32_Printer` 枚举；`_OpenPrinterByName()` / `_OpenPrinterDefault()` 必须用 `DllStructCreate("ptr")` 接句柄（AutoIt `ptr*` ByRef 无效）；`StartDocPrinterW` 依次尝试 `Level1+RAW → Level1+NULL → Level2+RAW`（第 2 参数是 Level，不是结构大小）；随后 `StartPagePrinter → WritePrinter → EndPagePrinter → EndDocPrinter → ClosePrinter`。
- fallback：`_PrintViaFallback()` 写 `%TEMP%\cut_label_<pid>.prn` + `%TEMP%\cut_rawprint.ps1`，PowerShell 里用 C# P/Invoke 再走一次 `OpenPrinter/StartDocPrinter(h,1,DI)/WritePrinter`；输出 `OK <bytes>` 视为成功，失败保留 `.prn` 供手动处理。全程写 `logs/client-YYYYMMDD.log`。
- `_PrintTaskLabel()` 打印成功后调用 `_ApiLogPrint()` 上报 `taskId/machineId/operator/labelNo/copies/result=success`；打印失败返回用户可理解的检查提示。

### 11.4 API 与字段映射现状（Api.au3）

- `_ApiAuth()` → `POST /api/device/auth`，body `machineNo + deviceKey`，返回 token。
- `_ApiTasks()` → `POST /api/data`，`action=cut_query&what=tasks&machineId=<gMachineNo>`。
- `_ApiStart()` → `POST /api/write`，`op=start_task` + `taskId/machineId`。
- `_ApiConfirm()` → `POST /api/write`，`op=confirm_task` + `taskId/machineId/operator/qty`（`qty` 为本次报产条数，缺省 1；设备端点「已完成」传整条剩余数量，服务端 `nextDone = min(quantity, done + qty)`，一次完成整条任务）。
- `_ApiLogPrint()` → `POST /api/write`，`op=log_print` + `taskId/machineId/operator/labelNo/copies/result`。
- 服务端 `lanhttpserver.cpp` 已实现上述 op；`cutLogPrint()` 写入 `cut_print_log(task_id,machine_id,operator,label_no,copies,result,created_at)`，字段与设备端一致。
- 任务二维数组共 22 列（`$T_COLS=22`，比主表格 16 列多 6 列内部字段）：`id, seq, batchName, circuit, material, section, color, wireLength, stripLeft, stripRight, terminalLeft, terminalRight, workLeft, workRight, quantity, doneQty, status, machineId, batchCreatedAt, operator, batchNo, taskNo`。
- 回退规则：`batchNo` 为空取 `batchName`；`taskNo` 为空取 `String(id)`；`operator` 为空取 `$gOperator`。

### 11.5 仍需处理

1. **[b30/b31 已调整]** 主界面列宽二次调整：移除左端子/右端子列，新增批次号列；b31 权重 `[30,110,140,60,40,60,70,40,40,60,150,150,30,30,70,80]`，任务号收窄、型号加宽，左右压前由 220 缩到 150。
2. 原生 ListView 无法真正单元格换行；若列宽调整后仍有省略号，下一步应考虑放弃原生 ListView（.NET DataGridView 曾在 AutoIt STA 下 COM 赋值失败，需另选自绘/第三方控件），不要继续在单元格换行上报希望。
3. 标签打印已可用，但需在现场 WinXP / Win10 两种下线机环境复测 fallback（PowerShell 可用性与 `.NET Add-Type` 权限）。
4. **[b28/b29/b30/b31/b32/b33 已完成]** 下线机 32/64 位版本、检查更新按 `arch` 取包、PC 设置页分别上传两种位数 exe、统一图标打包均已实现；PC Qt 本体仍只构建 64 位，没有引入 32 位 PC 包。b29 精简控件；b30 统一「型号 + 批次号」；b31 用自绘标签 + 单 ListView 彻底消除原生 Tab 白底并调整任务号/型号列宽；b32 统一中文包名上传规则、配置/log 迁移到 D 盘、派单支持「其他」机台；b33 接管 COM 异常并在服务器连不上时弹中文提示。

### 11.6 派单规则与「其他」机台（b32 起）

- `cut_machine` 启动时自动补一条 `code='OTHER'`、`name='其他'` 的记录，按 id 排在五台机之后；其它机台 `active=1`，「其他」不参与智能均衡，只作人工/手动下线目标。
- PC「派单规则」页的目标机台下拉**包含「其他」**，可以把任意字段规则（如 `端子 包含 屏蔽`）指向它；`cutComputeAssignments()` 的 `ruleTargetValid()` 允许硬规则命中 OTHER，不再因为不在 `active` 列表里被丢弃。
- **内置规则**：`cuttypes.h` 的 `cutItemHasShield()` 检查端子 / 防水栓（压前作业）/ 材质任一列含「屏蔽」，智能分配第一遍直接归「其他」，优先于用户规则，无需手工建规则。Excel「机台」列写「其他/人工/手动/OTHER」也会解析到该机台。
- 预览机台分布顺序：一号机→五号机→其他；未分配(0)并入「其他」显示。
