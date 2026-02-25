# demoHostAndDspService

> OpenHarmony 6.0 最小 out-of-process 离线音频处理 Demo

本项目演示了在 OpenHarmony 平台上通过 **IPC（Binder）+ Ashmem（共享内存）** 实现跨进程离线音频处理的完整链路：
- **控制面**：ArkTS `rpc.MessageSequence` + `ServiceExtensionAbility` IPC 调用
- **数据面**：`rpc.Ashmem` 匿名共享内存传递 float32 PCM 音频数据

---

## 目录结构

```
demoHostAndDspService/
├── shared/
│   └── AudioSharedBuffer.h       # 共享内存 Header 结构体定义（两侧共用）
├── HostApp/                      # 宿主应用（DevEco Studio 工程）
│   ├── AppScope/                 # Bundle 级配置
│   ├── entry/
│   │   └── src/main/
│   │       ├── ets/
│   │       │   ├── entryability/ # UIAbility 入口
│   │       │   └── pages/        # Index.ets：UI 参数输入 + 处理触发
│   │       └── cpp/              # C++ native：正弦波生成 + WAV 写入
│   └── ...
└── DspService/                   # DSP 服务应用（独立进程，DevEco Studio 工程）
    ├── AppScope/
    ├── entry/
    │   └── src/main/
    │       ├── ets/
    │       │   └── serviceextability/ # DspServiceExtAbility.ets：IPC Stub
    │       └── cpp/                   # C++ native：增益处理 + soft clip
    └── ...
```

---

## 整体架构

```
┌──────────────────────────────────────────────────────────────────┐
│  HostApp 进程 (com.example.hostapp)                               │
│                                                                  │
│  [Index.ets UI]                                                  │
│       │ 1. 参数输入（采样率/帧数/增益/旁通）                      │
│       │ 2. 调 C++ native 生成正弦波 float32 PCM（ArrayBuffer）   │
│       │ 3. 创建 rpc.Ashmem，写入 Header + Input PCM             │
│       │ 4. connectServiceExtensionAbility → 获取 IPC Proxy      │
│       │                                                          │
│  [IPC Proxy]─────────── Binder IPC ─────────────────────────────┼──►
│       │                                                          │
│       │ 8. 从 Ashmem 读取 Output PCM                            │
│       │ 9. 调 C++ native 写 out.wav（PCM-16 WAV）               │
│       │ 10. UI 显示耗时和输出路径                                 │
└──────────────────────────────────────────────────────────────────┘
                                    │
                                    │ Binder IPC（rpc.MessageSequence）
                                    │   请求：ashmem fd + audioDesc + params
                                    │   应答：status + processingTimeNs
                                    ▼
┌──────────────────────────────────────────────────────────────────┐
│  DspService 进程 (com.example.dspservice / dsp_proc)             │
│                                                                  │
│  [DspServiceExtAbility → DspRemoteStub.onRemoteMessageRequest]  │
│       │ 5. 从 MessageSequence 读取 ashmem + 参数                 │
│       │ 6. mapReadAndWriteAshmem，读取 Input PCM                │
│       │ 7. 调 C++ native processAudio：                         │
│       │      bypass=true  → memcpy（直通）                      │
│       │      bypass=false → output = tanh(input * gain)         │
│       │    将 Output PCM 写回 Ashmem，更新 Header               │
│       │    IPC reply：status=0 + processingTimeNs               │
└──────────────────────────────────────────────────────────────────┘

共享内存布局（Ashmem，见 shared/AudioSharedBuffer.h）：
  [  0..127 ]  AudioSharedHeader（magic/version/sampleRate/channels/frames/
                  format/inputOffset/outputOffset/status/processingTimeNs/
                  gain/bypass）
  [128..128+N)  Input  float32 PCM（N = frames × channels × 4 字节）
  [128+N..128+2N) Output float32 PCM
```

---

## 技术要点

| 方面 | 技术选型 |
|------|----------|
| ArkTS ↔ C++ 桥接 | N-API（OpenHarmony 标准方式） |
| IPC 机制 | `rpc.MessageSequence` + `ServiceExtensionAbility`（type: `"service"`） |
| 共享内存 | `rpc.Ashmem.createAshmem` → 通过 `writeAshmem/readAshmem` 经 IPC 传递 fd |
| 音频格式 | float32 interleaved PCM（内部），PCM-16 WAV（最终输出） |
| DSP 算法 | `output = tanh(input × gain)`（soft clip 防溢出） |
| 独立进程 | DspService 和 HostApp 是不同 Bundle，天然运行在不同进程中（`process` 字段仅支持 PC/平板；`extensionProcessMode` 用于同 Bundle 内多实例场景，跨 Bundle 无需配置） |
| 服务类型选择 | 使用 `ServiceExtensionAbility`（type: `"service"`）而非 `AppServiceExtensionAbility`，无需在开发者控制台申请额外能力，调试自动签名即可运行 |

---

## 环境要求

- DevEco Studio 5.x（支持 OpenHarmony API 12 / HarmonyOS 5.0）
- OpenHarmony SDK API 12 或更高
- 真机：搭载 OpenHarmony 5.0 / HarmonyOS 5.0 的手机或平板
- 签名证书（见下方说明）

---

## 构建与运行步骤

### 1. 导入工程

在 DevEco Studio 中分别打开两个工程目录：

```
File → Open → 选择 HostApp/
File → Open → 选择 DspService/
```

> 两个工程相互独立，分别编译、签名、安装。

### 2. 配置签名（两个工程必须使用同一证书）

> ⚠️ **重要**：`AppServiceExtensionAbility` 跨 Bundle 连接要求调用方（HostApp）与服务方（DspService）**使用同一签名证书**。证书不一致会导致连接被系统拒绝。以下提供两种方案，推荐方案一。

#### 方案一：DevEco Studio 自动签名（推荐）

两个工程均使用同一个华为开发者账号进行自动签名，系统会自动为同一账号下的所有工程颁发来自同一根证书的调试证书。

**操作步骤**（两个工程分别执行，账号必须相同）：

1. 打开 HostApp 工程，依次进入：  
   **File → Project Structure → Project → Signing Configs**
2. 勾选 **"Automatically generate signature"**
3. 点击 **"Sign In"**，登录华为开发者账号（若已登录则跳过）
4. DevEco Studio 自动生成并填入 `Store file`、`Store password`、`Key alias`、`Key password`、`Profile file`、`Certpath file`
5. 点击 **OK** 保存
6. **切换到 DspService 工程**，用**同一账号**重复步骤 1–5

> **验证**：两个工程自动签名后，在各自的 `build/outputs/default/` 目录下会生成 `entry-default-signed.hap`。可通过以下命令确认证书指纹一致：
> ```bash
> # 解压两个 HAP（HAP 本质是 zip 文件）并对比证书
> unzip -p HostApp/entry/build/default/outputs/default/entry-default-signed.hap META-INF/CERT.RSA | \
>   keytool -printcert -v 2>/dev/null | grep "SHA256:"
>
> unzip -p DspService/entry/build/default/outputs/default/entry-default-signed.hap META-INF/CERT.RSA | \
>   keytool -printcert -v 2>/dev/null | grep "SHA256:"
> # 两行 SHA256 指纹应完全一致
> ```

#### 方案二：手动共享同一密钥库（离线 / CI 环境）

当无法使用自动签名（如 CI 流水线、无网环境）时，手动让两个工程引用同一个 `.p12` 密钥库文件。

**步骤**：

1. 在 HostApp 工程中，首次运行自动签名后，DevEco Studio 会在本地生成密钥库，默认路径为：
   ```
   %USERPROFILE%\.ohos\config\default\<账号ID>\<项目名>\entry\debug.p12   （Windows）
   ~/.ohos/config/default/<账号ID>/<项目名>/entry/debug.p12               （macOS/Linux）
   ```
2. 将上述 `.p12`、对应的 `.cer`（证书文件）和 `.p7b`（Profile 文件）**复制到两个工程均可访问的公共目录**，例如仓库根目录下的 `signing/` 文件夹（**切勿提交到 Git，已在 `.gitignore` 中排除**）
3. 在 HostApp 工程中进入 **File → Project Structure → Signing Configs**，**取消**勾选自动签名，手动填入：
   - `Store file`：指向共享的 `.p12` 文件
   - `Store password` / `Key alias` / `Key password`：与生成时一致
   - `Profile file`：指向共享的 `.p7b` 文件
   - `Certpath file`：指向共享的 `.cer` 文件
4. 在 **DspService 工程**中重复步骤 3，指向**完全相同**的文件

**安全提示**：密钥库文件包含私钥，**不得提交到版本控制系统**。请在 `.gitignore` 中保留：
```
signing/
*.p12
*.cer
*.p7b
```

#### 安装顺序

两种签名方案配置完成后，均须**先安装 DspService，再安装 HostApp**：

```bash
hdc install DspService/entry/build/default/outputs/default/entry-default-signed.hap
```

或在 DevEco Studio 中直接 **Run** DspService 工程（Ability 为 DspServiceExtAbility，无 UI，安装即可）。

### 3. 安装并运行 HostApp

```bash
hdc install HostApp/entry/build/default/outputs/default/entry-default-signed.hap
hdc shell aa start -a EntryAbility -b com.example.hostapp
```

或在 DevEco Studio 中直接 **Run** HostApp 工程。

### 4. 操作界面

1. 打开 HostApp，看到参数输入界面；
2. 按需调整采样率（默认 44100）、帧数（默认 44100，即 1 秒）、增益（默认 0.5）、旁通开关；
3. 点击 **"处理音频"** 按钮；
4. 界面将依次显示：  
   - 正在连接 DspService…  
   - 处理完成（DSP 耗时 + IPC 往返时间）  
   - 输出文件路径（可长按复制）

---

## 产物 out.wav 的位置

| 获取方式 | 命令 / 说明 |
|----------|-------------|
| 路径 | `/data/app/el2/100/base/com.example.hostapp/files/out.wav` |
| hdc 拉取 | `hdc file recv /data/app/el2/100/base/com.example.hostapp/files/out.wav ./out.wav` |
| hilog 中显示 | HostApp 日志 `outputPath=...` |

> WAV 格式为 **PCM-16，双声道，44100 Hz**（按 UI 输入的参数生成）。  
> 在任意支持标准 WAV 的播放器中均可播放。

---

## 日志查看

```bash
# 实时查看 HostApp 日志
hdc shell hilog | grep HostApp

# 实时查看 DspService 日志
hdc shell hilog | grep DspServiceExtAbility

# 查看 HostApp 进程 PID
hdc shell ps -ef | grep com.example.hostapp

# 查看 DspService 进程 PID（应与 HostApp 不同）
hdc shell ps -ef | grep com.example.dspservice
```

---

## 常见问题

### 💡 为什么使用 `ServiceExtensionAbility` 而非 `AppServiceExtensionAbility`？

**结论：本 Demo 使用 `ServiceExtensionAbility`（type: `"service"`），无需访问华为开发者控制台，自动签名即可运行。**

| 对比项 | `AppServiceExtensionAbility` | `ServiceExtensionAbility` |
|--------|------------------------------|---------------------------|
| 注册类型 | `"appService"` | `"service"` |
| 连接 API | `connectServiceExtensionAbility` | `connectServiceExtensionAbility` |
| 是否需要开发者控制台开启 "AppService 服务" 能力 | **是** | **否** |
| 调试自动签名是否可用 | 需额外 Profile 能力 | **开箱即用** |
| 不满足能力要求时的报错 | `ability_context_impl.cpp:1599 failed 2097170` → `16000002` | 不会出现此错误 |

`AppServiceExtensionAbility` 是为生产发布应用（需在应用市场上架、通过华为认证）设计的；对于开发阶段 Demo，`ServiceExtensionAbility` 是正确选择。

---

### ❓ 连接 DspService 失败，错误码 16000002

切换到 `ServiceExtensionAbility` 后，此错误应不再出现。若仍出现，按以下步骤排查：

| # | 原因 | 验证方法 | 解决方法 |
|---|------|----------|----------|
| 1 | **设备上仍安装着旧版 DspService**（type 仍为 appService）| `hdc shell bm dump -n com.example.dspservice \| grep type` | `hdc uninstall com.example.dspservice`，重新构建安装新版 |
| 2 | **两个 HAP 签名账号不同** | DevEco Studio → File → Project Structure 确认两工程登录账号一致 | 两工程用同一华为账号自动签名，重新构建安装 |
| 3 | **安装顺序错误** | — | 先安装 DspService，再安装 HostApp |
| 4 | **同时开启两个调试会话** | 检查 DevEco Studio 是否为 DspService 也开了 Debug 标签 | DspService 只需安装，仅在 HostApp 工程启动调试 |
| 5 | **Hvigor / AMS 缓存** | 重新构建后仍报错 | 卸载两个应用，重启设备，重新安装 |

#### 快速诊断命令

```bash
# 1. 确认 DspService 已安装且 type 为 service（而非 appService）
hdc shell bm dump -n com.example.dspservice | grep -A5 "extensionAbilities"
# 预期：name: DspServiceExtAbility, type: service, exported: true

# 2. 确认两个进程均在运行
hdc shell ps -ef | grep "com.example"

# 3. 实时查看 AMS / HostApp 日志
hdc shell hilog | grep -E "HostApp|AbilityManagerService|ability_context"
```

---

### 🔍 历史分析：`[ability_context_impl.cpp:1599] failed 2097170` 的原因

> **此问题已通过将 DspService 改为 `ServiceExtensionAbility` 从根本上解决，以下内容仅供参考。**

当 DspService 使用 `AppServiceExtensionAbility`（type: `"appService"`）时，`connectServiceExtensionAbility` 请求到达 AMS 后，AMS 会检查调用方的 Provisioning Profile 中是否包含 "AppService 服务" 能力（capability entitlement）。若缺少此能力，AMS 在 `ability_context_impl.cpp:1599` 处以内部错误码 `2097170`（= AAFWK 子系统，module 0，errNo 18 = `ERR_CROSS_BUNDLE_CONNECT_PERMISSION_DENIED`）拒绝请求，最终映射至公开 API 错误 **16000002**。

该能力必须在华为开发者控制台（Developer Console）为应用显式开启后重新生成 Profile，**无法仅在 DevEco Studio 中完成**。因此本 Demo 改为使用 `ServiceExtensionAbility`，彻底绕过此鉴权路径。

---

### 其他常见问题

| 问题 | 原因 | 解决方法 |
|------|------|----------|
| 点击按钮后长时间显示"连接 DspService 失败" | DspService 未安装，或签名不匹配 | 先安装 DspService，确保签名配置正确 |
| IPC 请求失败，错误码：-1 | DspService 崩溃或拒绝连接 | 查看 DspService 的 hilog；检查 `exported: true` |
| out.wav 无声或噪音 | gain=0 或参数异常 | 确认增益 > 0，旁通未误开 |
| 找不到 out.wav | 写文件权限问题 | 文件写入 App 沙箱 `filesDir`，无需额外权限 |
| 两个进程 PID 相同 | DspService 与 HostApp 是不同 Bundle，正常情况下进程不同 | 确认两个工程均已安装且签名正确 |

---

## 文件说明

| 文件 | 说明 |
|------|------|
| `shared/AudioSharedBuffer.h` | 共享内存 Header C 结构体，两侧 C++ 代码共用 |
| `HostApp/.../Index.ets` | UI 逻辑，IPC 客户端，Ashmem 读写 |
| `HostApp/.../audio_native.cpp` | 正弦波生成、Header 序列化、WAV 写入 |
| `HostApp/.../napi_init.cpp` | N-API 桥接，暴露 3 个函数给 ArkTS |
| `DspService/.../DspServiceExtAbility.ets` | IPC Stub（AppServiceExtensionAbility），Ashmem 读写，调用 native |
| `DspService/.../dsp_processor.cpp` | 实际 DSP 算法（gain + tanh soft clip / bypass） |
| `DspService/.../dsp_napi.cpp` | N-API 桥接，暴露 1 个函数给 ArkTS |

---

## 参考文档

- [OpenHarmony IPC 开发指南](https://docs.openharmony.cn/pages/v5.0/zh-cn/application-dev/connectivity/ipc-rpc-overview.md)
- [ServiceExtensionAbility 开发指南](https://docs.openharmony.cn/pages/v5.0/zh-cn/application-dev/application-models/serviceextensionability.md)
- [N-API 开发指南](https://docs.openharmony.cn/pages/v5.0/zh-cn/application-dev/napi/napi-introduction.md)
- [Ashmem API 参考](https://docs.openharmony.cn/pages/v5.0/zh-cn/application-dev/reference/apis-ipc-kit/js-apis-rpc.md)