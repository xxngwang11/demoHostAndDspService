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
| 独立进程 | `module.json5` 中为 ServiceExtensionAbility 指定 `"process"` 字段 |

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

### 2. 配置签名

> ⚠️ **重要**：`ServiceExtensionAbility` 跨 Bundle 连接在 OpenHarmony 上需要特定签名配置。
> 
> - 在 DevEco Studio 中进入 **File → Project Structure → Signing Configs**，为两个工程分别配置调试签名（自动签名或自定义签名均可）。
> - 若在真机上遇到权限拒绝，需确认：  
>   1. 两个 HAP 使用同一签名证书（或受信任的证书链）；  
>   2. DspService 的 `module.json5` 中 ServiceExtensionAbility 已配置 `"exported": true`；  
>   3. 如设备为 OpenHarmony 标准版，可能需要在 `UnsgnedReleasedProfileTemplate.json` 中添加 `allowAppUsePrivilegeExtension: true`（参见官方文档）。

### 3. 安装 DspService（先安装）

```bash
hdc install DspService/entry/build/default/outputs/default/entry-default-signed.hap
```

或在 DevEco Studio 中直接 **Run** DspService 工程（Ability 为 DspServiceExtAbility，无 UI，安装即可）。

### 4. 安装并运行 HostApp

```bash
hdc install HostApp/entry/build/default/outputs/default/entry-default-signed.hap
hdc shell aa start -a EntryAbility -b com.example.hostapp
```

或在 DevEco Studio 中直接 **Run** HostApp 工程。

### 5. 操作界面

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

| 问题 | 原因 | 解决方法 |
|------|------|----------|
| 点击按钮后长时间显示"连接 DspService 失败" | DspService 未安装，或签名不匹配 | 先安装 DspService，确保签名配置正确 |
| IPC 请求失败，errCode=-1 | DspService 崩溃或拒绝连接 | 查看 DspService 的 hilog；检查 `exported: true` |
| out.wav 无声或噪音 | gain=0 或参数异常 | 确认增益 > 0，旁通未误开 |
| 找不到 out.wav | 写文件权限问题 | 文件写入 App 沙箱 `filesDir`，无需额外权限 |
| 两个进程 PID 相同 | DspService 未正确配置独立进程 | 检查 `module.json5` 中的 `"process"` 字段 |

---

## 文件说明

| 文件 | 说明 |
|------|------|
| `shared/AudioSharedBuffer.h` | 共享内存 Header C 结构体，两侧 C++ 代码共用 |
| `HostApp/.../Index.ets` | UI 逻辑，IPC 客户端，Ashmem 读写 |
| `HostApp/.../audio_native.cpp` | 正弦波生成、Header 序列化、WAV 写入 |
| `HostApp/.../napi_init.cpp` | N-API 桥接，暴露 3 个函数给 ArkTS |
| `DspService/.../DspServiceExtAbility.ets` | IPC Stub，Ashmem 读写，调用 native |
| `DspService/.../dsp_processor.cpp` | 实际 DSP 算法（gain + tanh soft clip / bypass） |
| `DspService/.../dsp_napi.cpp` | N-API 桥接，暴露 1 个函数给 ArkTS |

---

## 参考文档

- [OpenHarmony IPC 开发指南](https://docs.openharmony.cn/pages/v5.0/zh-cn/application-dev/connectivity/ipc-rpc-overview.md)
- [ServiceExtensionAbility 开发指南](https://docs.openharmony.cn/pages/v5.0/zh-cn/application-dev/application-models/serviceextensionability.md)
- [N-API 开发指南](https://docs.openharmony.cn/pages/v5.0/zh-cn/application-dev/napi/napi-introduction.md)
- [Ashmem API 参考](https://docs.openharmony.cn/pages/v5.0/zh-cn/application-dev/reference/apis-ipc-kit/js-apis-rpc.md)