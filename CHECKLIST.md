# Checklist — 验证 out-of-process 音频处理

本 Checklist 用于验证 HostApp + DspService Demo 的核心功能。

---

## ✅ 1. 验证 DspService 在独立进程中运行

**目的**：确认 DspService 的 ServiceExtensionAbility 在与 HostApp 不同的进程中运行。

**步骤**：
```bash
# 安装两个 HAP 并运行 HostApp 后，查询进程列表
hdc shell ps -ef | grep "com.example"
```

**预期结果**：
- 看到两条（或以上）进程记录，PID **不同**：
  ```
  u0_a100  1234  ...  com.example.hostapp
  u0_a101  5678  ...  com.example.dspservice:com.example.dspservice.dsp_proc
  ```
- `com.example.hostapp` 的 PID 与 `com.example.dspservice` 的 PID **不同** → ✅

**辅助验证（日志）**：
```bash
hdc shell hilog | grep -E "HostApp|DspServiceExtAbility"
```
- HostApp 日志中看到 `Connected to DspService`；
- DspService 日志中看到 `onCreate — DspService is UP (separate process)`。

---

## ✅ 2. 验证 DSP 崩溃不影响 HostApp（隔离性）

**目的**：证明 out-of-process 隔离——DSP 进程崩溃不会导致 HostApp 崩溃。

**步骤**：
```bash
# 1. 先让 HostApp 成功处理一次音频
# 2. 查找 DspService 进程 PID
hdc shell ps -ef | grep dspservice
# 3. 强杀 DspService 进程
hdc shell kill -9 <DSP_PID>
# 4. 回到 HostApp 再次点击"处理音频"
```

**预期结果**：
- HostApp **不崩溃**，而是在 UI 上显示 `❌ 连接失败` 或 IPC 错误信息；
- HostApp 进程 PID 保持不变 → ✅

---

## ✅ 3. 验证增益（gain）效果

**目的**：确认 DSP 算法正确应用增益。

**步骤**：
1. 设置 gain=**1.0**，bypass=**关**，点击"处理音频"，拉取并收听 out.wav；
2. 设置 gain=**0.1**，bypass=**关**，再次处理，拉取并收听第二个 wav；
3. 对比两次输出音量。

```bash
hdc file recv /data/app/el2/100/base/com.example.hostapp/files/out.wav ./out_gain1.wav
# 修改增益后再处理
hdc file recv /data/app/el2/100/base/com.example.hostapp/files/out.wav ./out_gain0.1.wav
```

**预期结果**：
- `out_gain0.1.wav` 音量明显低于 `out_gain1.wav` → ✅
- 可用 `ffprobe` 或 Audacity 查看音量包络确认。

---

## ✅ 4. 验证旁通（bypass）效果

**目的**：确认 bypass=true 时输出 == 输入（无处理）。

**步骤**：
1. 设置 gain=**2.0**，bypass=**关**，处理并保存 wav（应有 soft clip 失真）；
2. 设置 gain=**2.0**，bypass=**开**，处理并保存第二个 wav（应无失真）；
3. 对比两个 wav 的波形。

**预期结果**：
- bypass=开 时波形为干净的正弦波；
- bypass=关 时 gain=2.0 经 tanh 后波形轻微饱和 → ✅

---

## ✅ 5. 验证输出 WAV 可播放

**目的**：确认产物 out.wav 是标准 PCM-16 WAV，可在任意播放器中正常播放。

**步骤**：
```bash
# 拉取 WAV 文件
hdc file recv /data/app/el2/100/base/com.example.hostapp/files/out.wav ./out.wav

# 检查文件头（应显示 RIFF...WAVE...PCM）
xxd out.wav | head -5

# 使用 ffprobe 检查格式
ffprobe out.wav
# 预期输出：pcm_s16le, 44100 Hz, stereo
```

**预期结果**：
- `file out.wav` 输出包含 `RIFF (little-endian) data, WAVE audio, Microsoft PCM`；
- 用 Windows Media Player / VLC / Audacity 等可正常播放，听到 440 Hz 正弦音 → ✅

---

## ✅ 6. 验证共享内存 Header 被正确更新

**目的**：确认 DspService 在处理完成后更新了 Ashmem Header 中的 status 和 processingTimeNs。

**步骤**：
在 HostApp 的 `Index.ets` 中，IPC 调用成功后读取 Header（偏移 0 的 128 字节）并打印 status（偏移 32）和 processingTimeNs（偏移 36），通过 hilog 观察：

```bash
hdc shell hilog | grep HostApp
```

**预期结果**：
- 日志中看到 processingTimeNs > 0；
- DSP 处理耗时在 UI 上正确显示为毫秒级数值 → ✅

---

## ✅ 7. 验证 IPC 传参正确性

**目的**：通过改变采样率确认参数正确传递。

**步骤**：
1. 设置 sampleRate=**22050**，frames=**22050**（1 秒），处理并拉取 wav；
2. 检查 wav 文件头中的采样率字段。

```bash
# WAV 头偏移 24 处为 uint32 采样率
python3 -c "
import struct
with open('out.wav','rb') as f:
    f.seek(24)
    sr = struct.unpack('<I', f.read(4))[0]
    print(f'Sample rate: {sr}')
"
```

**预期结果**：
- 输出 `Sample rate: 22050` → ✅

---

## 📝 快速验证脚本

```bash
#!/bin/bash
echo "=== 验证进程隔离 ==="
hdc shell ps -ef | grep "com.example" | awk '{print $1, $2, $NF}'

echo ""
echo "=== 拉取输出文件 ==="
hdc file recv /data/app/el2/100/base/com.example.hostapp/files/out.wav ./out.wav
ls -lh out.wav

echo ""
echo "=== 检查 WAV 头 ==="
xxd out.wav | head -3

echo ""
echo "=== 最新 hilog（最近 50 行） ==="
hdc shell hilog | grep -E "HostApp|DspService" | tail -50
```

---

## 关键结论

| 验证项 | 验证方法 | 期望 |
|--------|----------|------|
| 独立进程 | `ps -ef \| grep com.example` | 两个不同 PID |
| DSP 崩溃隔离 | kill DSP 进程后 HostApp 不崩 | HostApp 显示错误但存活 |
| gain 生效 | 对比不同 gain 的 wav 音量 | 音量随 gain 变化 |
| bypass 生效 | bypass=true 时波形无失真 | 正弦波形完整 |
| 输出可播放 | ffprobe / 播放器 | 标准 PCM-16 WAV |
| Header 更新 | hilog 观察 processingTimeNs | > 0 |
