/**
 * Index.ets — HostApp 主页面
 *
 * 控制面（IPC）：通过 connectServiceExtensionAbility + rpc.MessageSequence 调用 DspService
 * 数据面（Ashmem）：通过 rpc.Ashmem 传递共享内存 fd，内含 input/output float32 PCM
 *
 * 共享内存布局（见 shared/AudioSharedBuffer.h）：
 *   [Header 128字节] [Input PCM float32] [Output PCM float32]
 */

import { rpc } from '@kit.IPCKit';
import { common, Want } from '@kit.AbilityKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import hostNative from 'libhostapp.so';

const TAG = 'HostApp';

/** DspService 的 Bundle / Ability 标识，需与 DspService module.json5 保持一致 */
const DSP_BUNDLE_NAME = 'com.example.dspservice';
const DSP_ABILITY_NAME = 'DspServiceExtAbility';

/** IPC 请求码，需与 DspServiceExtAbility 中定义相同 */
const PROCESS_AUDIO_CODE = 1001;

/** 共享内存 Header 固定大小（字节），与 C++ AUDIO_SHM_HEADER_SIZE 保持一致 */
const HEADER_SIZE = 128;

@Entry
@Component
struct Index {
  /* -------- 表单参数 -------- */
  @State sampleRateStr: string = '44100';
  @State framesStr: string = '44100';
  @State gainStr: string = '0.5';
  @State bypass: boolean = false;

  /* -------- 结果显示 -------- */
  @State statusText: string = '就绪，请点击"处理音频"按钮';
  @State outputPath: string = '';
  @State processingTimeMs: string = '';
  @State isProcessing: boolean = false;

  /* -------- IPC 连接状态 -------- */
  private connectionId: number = -1;
  private remoteProxy: rpc.IRemoteObject | null = null;
  private context = getContext(this) as common.UIAbilityContext;

  /**
   * 连接 DspService（ServiceExtensionAbility）。
   * 返回 Promise，onConnect 触发后 resolve。
   */
  private connectDsp(): Promise<void> {
    return new Promise<void>((resolve, reject) => {
      const want: Want = {
        bundleName: DSP_BUNDLE_NAME,
        abilityName: DSP_ABILITY_NAME,
      };
      const opts: common.ConnectOptions = {
        onConnect: (_element: object, proxy: rpc.IRemoteObject) => {
          hilog.info(0x0000, TAG, 'onConnect — DspService connected');
          this.remoteProxy = proxy;
          resolve();
        },
        onDisconnect: (_element: object) => {
          hilog.warn(0x0000, TAG, 'DspService disconnected');
          this.remoteProxy = null;
        },
        onFailed: (code: number) => {
          hilog.error(0x0000, TAG, 'connectServiceExtensionAbility failed, code=%{public}d', code);
          reject(new Error(`连接 DspService 失败，错误码: ${code}`));
        },
      };
      this.connectionId = this.context.connectServiceExtensionAbility(want, opts);
      hilog.info(0x0000, TAG, 'connectServiceExtensionAbility called, connId=%{public}d', this.connectionId);
    });
  }

  /**
   * 完整的离线音频处理流程：
   *  1. 调 C++ native 生成正弦波 PCM (float32)
   *  2. 创建 Ashmem，写入 Header + Input PCM
   *  3. 连接 DspService（如尚未连接）
   *  4. 通过 IPC 调 DspService.ProcessSharedBuffer
   *  5. 从 Ashmem 读取 Output PCM
   *  6. 调 C++ native 将 Output 写成 out.wav (PCM16)
   */
  private async processAudio(): Promise<void> {
    this.isProcessing = true;
    this.statusText = '正在处理…';
    this.outputPath = '';
    this.processingTimeMs = '';

    try {
      /* ---------- 解析参数 ---------- */
      const sr = Math.max(1, parseInt(this.sampleRateStr) || 44100);
      const frm = Math.max(1, parseInt(this.framesStr) || 44100);
      const ch = 2; // 固定双声道
      const gain = parseFloat(this.gainStr) || 0.5;
      const bypassInt = this.bypass ? 1 : 0;
      const pcmBytes = frm * ch * 4; // float32 大小

      hilog.info(0x0000, TAG, 'params: sr=%{public}d frm=%{public}d ch=%{public}d gain=%{public}f bypass=%{public}d',
        sr, frm, ch, gain, bypassInt);

      /* ---------- Step 1：生成正弦波 ---------- */
      const inputAb: ArrayBuffer = hostNative.generateSineWave(sr, frm, ch, 440.0);
      hilog.info(0x0000, TAG, 'sine wave generated, byteLen=%{public}d', inputAb.byteLength);

      /* ---------- Step 2：创建 Ashmem ---------- */
      const totalSize = HEADER_SIZE + pcmBytes * 2;
      const ashmem = rpc.Ashmem.createAshmem('audio_proc_shm', totalSize);
      ashmem.mapReadAndWriteAshmem();

      // 写入 Header（128 字节）
      const headerBytes: number[] = hostNative.buildHeader(sr, ch, frm, gain, bypassInt);
      ashmem.writeToAshmem(headerBytes, headerBytes.length, 0);

      // 写入 Input PCM（紧接 Header）
      const inputU8 = new Uint8Array(inputAb);
      const inputArr: number[] = Array.from(inputU8);
      ashmem.writeToAshmem(inputArr, inputArr.length, HEADER_SIZE);

      /* ---------- Step 3：确保已连接 DspService ---------- */
      if (!this.remoteProxy) {
        this.statusText = '正在连接 DspService…';
        await this.connectDsp();
      }

      /* ---------- Step 4：IPC 调用 ---------- */
      const data = rpc.MessageSequence.create();
      const reply = rpc.MessageSequence.create();
      const option = new rpc.MessageOption();

      data.writeAshmem(ashmem);
      data.writeInt(sr);
      data.writeInt(frm);
      data.writeInt(ch);
      data.writeFloat(gain);
      data.writeInt(bypassInt);

      const t0 = Date.now();
      const rr = await this.remoteProxy!.sendMessageRequest(PROCESS_AUDIO_CODE, data, reply, option);
      const ipcMs = Date.now() - t0;

      data.reclaim();

      if (rr.errCode !== 0) {
        rr.reply.reclaim();
        throw new Error(`IPC 请求失败，errCode=${rr.errCode}`);
      }

      const dspStatus = rr.reply.readInt();
      // readLong() returns JS number (IEEE 754 double). Precise for values up to
      // 2^53-1 ns ≈ 104 days; more than sufficient for any practical DSP buffer.
      const dspTimeNs = rr.reply.readLong();
      rr.reply.reclaim();

      if (dspStatus !== 0) {
        throw new Error(`DspService 处理失败，status=${dspStatus}`);
      }

      hilog.info(0x0000, TAG, 'IPC done, dspTimeNs=%{public}d ipcMs=%{public}d', dspTimeNs, ipcMs);

      /* ---------- Step 5：读取 Output PCM ---------- */
      const outputArr: number[] = ashmem.readFromAshmem(pcmBytes, HEADER_SIZE + pcmBytes);
      const outputAb: ArrayBuffer = new Uint8Array(outputArr).buffer;

      ashmem.unmapAshmem();
      ashmem.closeAshmem();

      /* ---------- Step 6：写 WAV 文件 ---------- */
      const outPath = this.context.filesDir + '/out.wav';
      const ok: boolean = hostNative.writeWavFile(outPath, outputAb, sr, ch, frm);
      if (!ok) {
        throw new Error('写入 WAV 文件失败');
      }

      const dspMs = (dspTimeNs / 1_000_000).toFixed(3);
      this.processingTimeMs = `DSP 处理耗时: ${dspMs} ms（IPC 往返: ${ipcMs} ms）`;
      this.outputPath = outPath;
      this.statusText = '✅ 处理完成';

    } catch (err) {
      const msg = (err as Error).message ?? String(err);
      this.statusText = `❌ 错误: ${msg}`;
      hilog.error(0x0000, TAG, 'processAudio error: %{public}s', msg);
    } finally {
      this.isProcessing = false;
    }
  }

  build() {
    Scroll() {
      Column({ space: 16 }) {

        Text('离线音频处理 Demo')
          .fontSize(22)
          .fontWeight(FontWeight.Bold)
          .margin({ top: 24, bottom: 8 })

        /* ---- 参数输入区 ---- */
        Column({ space: 8 }) {
          Row() {
            Text('采样率 (Hz)').width('42%').fontSize(15)
            TextInput({ text: this.sampleRateStr, placeholder: '44100' })
              .width('55%')
              .type(InputType.Number)
              .onChange((v: string) => {
                this.sampleRateStr = v;
              })
          }.width('92%')

          Row() {
            Text('帧数').width('42%').fontSize(15)
            TextInput({ text: this.framesStr, placeholder: '44100' })
              .width('55%')
              .type(InputType.Number)
              .onChange((v: string) => {
                this.framesStr = v;
              })
          }.width('92%')

          Row() {
            Text('声道数').width('42%').fontSize(15)
            Text('2（固定立体声）').width('55%').fontSize(15).fontColor('#666')
          }.width('92%')

          Row() {
            Text('增益 Gain (0~2.0)').width('42%').fontSize(15)
            TextInput({ text: this.gainStr, placeholder: '0.5' })
              .width('55%')
              .onChange((v: string) => {
                this.gainStr = v;
              })
          }.width('92%')

          Row() {
            Text('旁通 Bypass').width('42%').fontSize(15)
            Toggle({ type: ToggleType.Switch, isOn: this.bypass })
              .width('55%')
              .onChange((v: boolean) => {
                this.bypass = v;
              })
          }.width('92%')
        }
        .backgroundColor('#F5F5F5')
        .borderRadius(12)
        .padding(12)
        .width('92%')

        /* ---- 操作按钮 ---- */
        Button(this.isProcessing ? '处理中…' : '处理音频')
          .width('92%')
          .height(50)
          .fontSize(17)
          .fontWeight(FontWeight.Medium)
          .enabled(!this.isProcessing)
          .backgroundColor(this.isProcessing ? '#A0CFFF' : '#007DFF')
          .onClick(() => {
            this.processAudio();
          })

        /* ---- 结果展示区 ---- */
        Column({ space: 8 }) {
          Text(this.statusText)
            .width('92%')
            .fontSize(14)
            .fontColor(this.statusText.startsWith('❌') ? Color.Red : Color.Black)
            .textAlign(TextAlign.Start)

          if (this.processingTimeMs !== '') {
            Text(this.processingTimeMs)
              .width('92%')
              .fontSize(13)
              .fontColor('#0A7')
              .textAlign(TextAlign.Start)
          }

          if (this.outputPath !== '') {
            Text('输出文件路径:')
              .width('92%')
              .fontSize(13)
              .fontColor('#555')
            Text(this.outputPath)
              .width('92%')
              .fontSize(12)
              .fontColor('#0057D8')
              .textAlign(TextAlign.Start)
              .copyOption(CopyOptions.LocalDevice)
          }
        }
        .width('100%')
        .alignItems(HorizontalAlign.Start)
        .padding({ left: 16, right: 16 })

      }
      .width('100%')
      .alignItems(HorizontalAlign.Center)
      .padding({ bottom: 40 })
    }
    .width('100%')
    .height('100%')
  }
}
