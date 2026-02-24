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
| IPC 机制 | `rpc.MessageSequence` + `ServiceExtensionAbility` |
| 共享内存 | `rpc.Ashmem.createAshmem` → 通过 `writeAshmem/readAshmem` 经 IPC 传递 fd |
| 音频格式 | float32 interleaved PCM（内部），PCM-16 WAV（最终输出） |
| DSP 算法 | `output = tanh(input × gain)`（soft clip 防溢出） |
| 独立进程 | DspService 和 HostApp 是不同 Bundle，天然运行在不同进程中（`process` 字段仅支持 PC/平板；`extensionProcessMode` 用于同 Bundle 内多实例场景，跨 Bundle 无需配置） |

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

### ❓ 连接 DspService 失败，错误码 16000002

错误码 16000002（`ERR_ABILITY_TYPE_INVALID`）由 AMS（Ability 管理服务）返回，表示"找到了目标 Ability，但类型不符"。  
以下按可能性从高到低列出所有已知原因及解决方法：

| # | 原因 | 验证方法 | 解决方法 |
|---|------|----------|----------|
| 1 | **Want 中包含 `moduleName` 字段** | 检查 `HostApp/Index.ets` 的 `connectDsp()` | 从 Want 中删除 `moduleName`，仅保留 `bundleName` + `abilityName` |
| 2 | **DspService 旧版 HAP 仍在设备上**（注册信息未刷新）| `hdc shell bm dump -n com.example.dspservice` 检查 extensionAbilities | `hdc uninstall com.example.dspservice` 彻底卸载后重新安装 |
| 3 | **两个 HAP 的 Debug/Release 模式不一致** | 检查 DevEco Studio 当前构建变体（左下角或 Build → Select Build Variant） | 两个工程均须以 **Debug** 模式构建签名；切勿将 Release HAP 与 Debug HAP 混合安装 |
| 4 | **同时开启两个 DevEco Studio 调试会话** | 观察 DevEco Studio 是否为 DspService 也开了 Debug 标签页 | DspService 只需安装（Run 一次使其进设备即可），之后**仅在 HostApp 工程中**启动调试；DspService 无需保持调试状态 |
| 5 | **设备 AMS 缓存未刷新** | 卸载重装后仍 16000002 | 完成上述步骤后**重启设备**，再重新安装并测试 |
| 6 | **DspService 的 `module.json5` 中 `extensionProcessMode` 字段残留** | 检查 `DspService/entry/src/main/module.json5` | 删除 `extensionProcessMode` 字段（该字段仅用于同 Bundle 多实例场景，跨 Bundle 无效且会触发 16000002） |
| 7 | **Hvigor 构建缓存损坏** | 清理后 rebuilt 报错 | DevEco Studio → **File → Invalidate Caches → Invalidate and Restart**，然后重新构建 |
| 8 | **设备 HarmonyOS 版本不支持** | `hdc shell param get const.ohos.apiversion` 查看 API 版本 | `AppServiceExtensionAbility` 跨 Bundle 连接需要 HarmonyOS 5.0（API 12）或更高；旧版设备不支持 |

#### 快速诊断命令

```bash
# 1. 确认 DspService 已安装且 DspServiceExtAbility 已注册
hdc shell bm dump -n com.example.dspservice | grep -A5 "extensionAbilities"
# 预期：看到 name: DspServiceExtAbility, type: appService, exported: true

# 2. 确认两个进程已运行（DspService 连接时才会启动）
hdc shell ps -ef | grep "com.example"

# 3. 实时查看 AMS / HostApp 错误日志
hdc shell hilog | grep -E "HostApp|AbilityManagerService|AMS"
```

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