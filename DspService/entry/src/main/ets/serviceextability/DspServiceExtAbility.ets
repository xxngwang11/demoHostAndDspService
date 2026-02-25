/**
 * DspServiceExtAbility.ets — DSP Service (独立进程侧)
 *
 * 以 ServiceExtensionAbility（type: "service"）运行，供第三方应用跨进程调用。
 * 使用 ServiceExtensionAbility 无需在华为开发者控制台申请任何额外能力（capability），
 * 调试签名（自动签名 Debug 模式）即可正常工作。
 *
 * IPC 接口（通过 rpc.RemoteObject）:
 *   请求码 PROCESS_AUDIO_CODE (1001)
 *   请求参数 (MessageSequence):
 *     writeAshmem  ashmem   — 共享内存（Header + Input PCM + Output PCM 区域）
 *     writeInt     sampleRate
 *     writeInt     frames
 *     writeInt     channels
 *     writeFloat   gain
 *     writeInt     bypass   (0=处理, 1=旁通)
 *   应答参数 (reply MessageSequence):
 *     writeInt     status   (0=成功, <0=错误)
 *     writeLong    processingTimeNs
 *
 * 共享内存布局（见 shared/AudioSharedBuffer.h）:
 *   [0..127]          Header (AudioSharedHeader)
 *   [128..128+pcmSz)  Input  PCM (float32 interleaved)
 *   [128+pcmSz..]     Output PCM (float32 interleaved)
 */

import { ServiceExtensionAbility, Want } from '@kit.AbilityKit';
import { rpc } from '@kit.IPCKit';
import { hilog } from '@kit.PerformanceAnalysisKit';
import dspNative from 'libdspservice.so';

const TAG = 'DspServiceExtAbility';

/** IPC 请求码，与 HostApp Index.ets 中定义保持一致 */
const PROCESS_AUDIO_CODE = 1001;

/** Ashmem 中 Header 固定大小（字节） */
const HEADER_SIZE = 128;

/* ------------------------------------------------------------------ */
/*  IPC Stub                                                            */
/* ------------------------------------------------------------------ */

class DspRemoteStub extends rpc.RemoteObject {
  constructor() {
    super('DspRemoteStub');
  }

  /**
   * 处理来自 HostApp 的 IPC 请求。
   * 同步实现：直接在 Binder 线程上完成 DSP 处理后返回。
   */
  onRemoteMessageRequest(
    code: number,
    data: rpc.MessageSequence,
    reply: rpc.MessageSequence,
    _option: rpc.MessageOption
  ): boolean | Promise<boolean> {

    hilog.info(0x0000, TAG, 'onRemoteMessageRequest code=%{public}d', code);

    if (code !== PROCESS_AUDIO_CODE) {
      return false;
    }

    try {
      /* ---- 读取 IPC 参数 ---- */
      const ashmem    = data.readAshmem();
      const sampleRate = data.readInt();
      const frames     = data.readInt();
      const channels   = data.readInt();
      const gain       = data.readFloat();
      const bypass     = data.readInt();

      hilog.info(0x0000, TAG,
        'params sr=%{public}d frm=%{public}d ch=%{public}d gain=%{public}f bypass=%{public}d',
        sampleRate, frames, channels, gain, bypass);

      const pcmBytes = frames * channels * 4; // float32

      /* ---- 映射 Ashmem ---- */
      ashmem.mapReadAndWriteAshmem();

      /* ---- 读取 Input PCM（紧接 Header） ---- */
      const inputArr: number[] = ashmem.readFromAshmem(pcmBytes, HEADER_SIZE);
      const inputAb = new Uint8Array(inputArr).buffer;

      /* ---- 调用 C++ DSP 处理 ---- */
      const result = dspNative.processAudio(inputAb, gain, bypass);

      const outputAb = result.outputBuffer;
      const processingTimeNs: number = result.processingTimeNs;

      /* ---- 将 Output PCM 写回 Ashmem ---- */
      const outputArr: number[] = Array.from(new Uint8Array(outputAb));
      ashmem.writeToAshmem(outputArr, outputArr.length, HEADER_SIZE + pcmBytes);

      /* ---- 更新 Header：status = DONE，processingTimeNs ---- */
      const headerArr: number[] = ashmem.readFromAshmem(HEADER_SIZE, 0);
      const headerBuf = new Uint8Array(headerArr).buffer;
      const view = new DataView(headerBuf);
      // status at byte offset 32 (int32, little-endian)
      view.setInt32(32, 2, true);
      // processingTimeNs at byte offset 36 (int64, split into two uint32).
      // Use arithmetic instead of bitwise ops to avoid 32-bit truncation for
      // values > 2^31. For typical DSP times (< 2^31 ns ≈ 2.1 s) both yield
      // identical results; using Math.floor + % is always safe.
      const TWO32 = 4294967296; // 2^32
      const loUnsigned = processingTimeNs % TWO32;           // lower 32 bits
      const hiUnsigned = Math.floor(processingTimeNs / TWO32); // upper 32 bits
      view.setUint32(36, loUnsigned, true);
      view.setUint32(40, hiUnsigned, true);
      const updatedHeader: number[] = Array.from(new Uint8Array(headerBuf));
      ashmem.writeToAshmem(updatedHeader, updatedHeader.length, 0);

      ashmem.unmapAshmem();
      ashmem.closeAshmem();

      /* ---- IPC 应答 ---- */
      reply.writeInt(0);                   // status = OK
      reply.writeLong(processingTimeNs);   // DSP 处理耗时 (ns)

      hilog.info(0x0000, TAG, 'processing done, timeNs=%{public}d', processingTimeNs);

    } catch (err) {
      const msg = (err as Error).message ?? String(err);
      hilog.error(0x0000, TAG, 'processing error: %{public}s', msg);
      reply.writeInt(-1);
      reply.writeLong(0);
    }

    return true;
  }
}

/* ------------------------------------------------------------------ */
/*  ServiceExtensionAbility                                             */
/* ------------------------------------------------------------------ */

export default class DspServiceExtAbility extends ServiceExtensionAbility {

  private stub: DspRemoteStub = new DspRemoteStub();

  onCreate(want: Want): void {
    hilog.info(0x0000, TAG, 'onCreate — DspService is UP (separate process)');
  }

  /** 返回 IPC Stub，HostApp 将获得此对象的 Proxy */
  onConnect(want: Want): rpc.RemoteObject {
    hilog.info(0x0000, TAG, 'onConnect');
    return this.stub;
  }

  onDisconnect(want: Want): void {
    hilog.info(0x0000, TAG, 'onDisconnect');
  }

  onDestroy(): void {
    hilog.info(0x0000, TAG, 'onDestroy');
  }
}
