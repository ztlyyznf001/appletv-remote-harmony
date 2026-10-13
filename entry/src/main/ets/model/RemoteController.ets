import { common } from '@kit.AbilityKit';
import { preferences } from '@kit.ArkData';
import * as companion from 'libcompanion.so';
import { CompanionError } from 'libcompanion.so';
import { Device, manualEndpoint } from './Device';
import { RemoteState } from './RemoteState';
import { TouchGesturePump, TouchSample } from './TouchGesturePump';
import DeviceDiscovery from '../discovery/DeviceDiscovery';
import CredentialStore from '../storage/CredentialStore';

export class RemoteController {
  readonly state: RemoteState = new RemoteState();
  private credentials: CredentialStore | undefined = undefined;
  private settings: preferences.Preferences | undefined = undefined;
  private discovery: DeviceDiscovery | undefined = undefined;
  private lastDevice: Device | undefined = undefined;
  private reconnectEnabled: boolean = false;
  private reconnectAttempts: number = 0;
  private reconnectTimer: number = -1;
  private initialized: boolean = false;
  private touchPump: TouchGesturePump | undefined = undefined;
  private touchHeld: boolean = false;
  private touchHoldWait: Promise<void> | undefined = undefined;
  private touchReleaseTimer: number = -1;
  private touchReleaseResolve: (() => void) | undefined = undefined;

  async initialize(context: common.UIAbilityContext): Promise<void> {
    if (this.initialized) return;
    this.initialized = true;
    this.discovery = new DeviceDiscovery(context, (devices: Device[], message: string) => {
      this.state.devices = devices; this.state.discoveryText = message;
    });
    try {
      this.settings = preferences.getPreferencesSync(context, { name: 'companion_settings_v1' });
      this.credentials = new CredentialStore(context);
      await this.credentials.initialize();
      const id: string = this.settings.getSync('last_id', '') as string;
      const name: string = this.settings.getSync('last_name', '') as string;
      const host: string = this.settings.getSync('last_host', '') as string;
      const port: number = this.settings.getSync('last_port', 0) as number;
      if (id.length > 0 && id.length <= 300 && host.length > 0 && host.length <= 128 && Number.isInteger(port) && port >= 1 && port <= 65535) {
        this.lastDevice = new Device(id, name, host, port);
        this.state.selected = this.lastDevice;
      }
      this.reconnectEnabled = this.settings.getSync('resume_connection', false) as boolean;
      this.state.ready = true;
      this.refreshForget();
      if (this.state.foreground) { this.discovery.start(); this.scheduleReconnect(); }
    } catch (_) {
      this.state.ready = false;
      this.state.status = '安全存储初始化失败，连接已停用。若密钥已丢失，可在系统设置清除此应用数据后重新配对；此操作会移除全部本机配对。';
    }
  }

  enterForeground(): void {
    if (this.state.foreground) return;
    this.state.foreground = true; this.reconnectAttempts = 0;
    if (this.state.ready) { this.discovery?.start(); this.scheduleReconnect(); }
  }

  leaveForeground(): void {
    this.state.foreground = false;
    this.discovery?.stop();
    this.state.devices = [];
    this.state.discoveryText = '后台已停止发现；返回后重新发现。';
    this.stopSession(false);
  }

  refreshDiscovery(): void {
    if (!this.state.foreground || !this.state.ready) return;
    this.discovery?.stop(); this.discovery?.start();
  }

  select(device: Device): void {
    if (!this.state.foreground || !this.state.ready || this.state.busy) return;
    this.stopSession(true);
    this.state.selected = device;
    this.state.status = '已选择设备；点击连接并核对电视上的 PIN。';
    this.refreshForget();
  }

  selectManual(address: string, port: string): boolean {
    const device: Device | undefined = manualEndpoint(address, port);
    if (device === undefined) {
      this.state.status = '请填写有效的数字 IPv4 / IPv6 地址和 1–65535 的实际 Companion 端口；IPv6 区域编号仅支持数字。';
      return false;
    }
    this.select(device);
    return true;
  }

  async connectSelected(): Promise<void> {
    if (this.state.connected || this.state.needsPin || this.state.busy) return;
    const target: Device | undefined = this.state.selected;
    if (target !== undefined) await this.connectDevice(target, false);
  }

  private async connectDevice(target: Device, automatic: boolean): Promise<void> {
    const store: CredentialStore | undefined = this.credentials;
    if (!this.state.foreground || !this.state.ready || this.state.busy || store === undefined) return;
    this.clearReconnectTimer();
    const epoch: number = ++this.state.generation;
    this.state.busy = true; this.state.connected = false;
    this.state.status = automatic ? '正在恢复已验证的连接…' : '正在建立安全连接…';
    let identity: ArrayBuffer | undefined = undefined;
    let loaded: boolean = false;
    try {
      try { identity = await store.load(target.id); loaded = true; }
      catch (_) {
        if (this.current(epoch)) this.state.status = '配对身份无法解密或已损坏。不会降级为明文；可确认忘记设备后重新配对。';
        return;
      }
      if (!this.current(epoch)) return;
      if (identity === undefined) {
        // Reconnection must never start a new pairing flow without the user's explicit action.
        if (automatic) { this.reconnectEnabled = false; return; }
        await companion.beginPairing(target.host, target.port);
        if (!this.current(epoch)) return;
        this.state.needsPin = true;
        this.state.status = '输入电视显示的 4 位 PIN；不要使用其他设备上的代码。';
      } else {
        await companion.connect(target.host, target.port, identity);
        if (!this.current(epoch)) return;
        this.connected(target);
      }
    } catch (error) {
      if (!this.current(epoch)) return;
      const code: string = (error as CompanionError).code;
      companion.cancel();
      this.state.connected = false;
      this.state.status = this.errorMessage(code);
      if (automatic && loaded && this.networkRetryable(code)) this.scheduleReconnect();
    } finally {
      if (identity !== undefined) new Uint8Array(identity).fill(0);
      if (this.current(epoch)) this.state.busy = false;
      // A failure can only schedule after busy clears, and never retries authentication errors.
    }
  }

  async finishPairing(): Promise<void> {
    const target: Device | undefined = this.state.selected;
    const store: CredentialStore | undefined = this.credentials;
    if (!this.state.needsPin || this.state.busy || target === undefined || store === undefined || !this.state.foreground) return;
    if (!/^[0-9]{4}$/.test(this.state.pin)) { this.state.status = 'PIN 必须是 4 位数字。'; return; }
    const pin: string = this.state.pin;
    this.state.pin = ''; // PIN is never persisted or logged. Immutable runtime strings cannot be securely overwritten.
    const epoch: number = this.state.generation;
    this.state.busy = true; this.state.status = '正在验证 PIN 和设备身份…';
    let identity: ArrayBuffer | undefined = undefined;
    try {
      identity = await companion.finishPairing(pin);
      if (!this.current(epoch)) return;
      this.state.needsPin = false;
      let saved: boolean = false;
      try { saved = await store.save(target.id, identity, () => this.current(epoch)); }
      catch (_) {
        if (this.current(epoch)) {
          companion.cancel();
          this.state.status = '配对成功，但无法安全保存身份。连接已关闭；请检查系统密钥库和存储后重试。';
        }
        return;
      }
      if (!saved || !this.current(epoch)) return;
      this.refreshForget();
      this.state.status = '身份已加密保存；正在验证加密会话…';
      await companion.connect(target.host, target.port, identity);
      if (this.current(epoch)) this.connected(target);
    } catch (error) {
      if (!this.current(epoch)) return;
      companion.cancel();
      this.state.connected = false; this.state.needsPin = false;
      this.state.status = this.errorMessage((error as CompanionError).code);
    } finally {
      if (identity !== undefined) new Uint8Array(identity).fill(0);
      if (this.current(epoch)) { this.state.busy = false; this.state.pin = ''; }
    }
  }

  async press(code: number): Promise<void> {
    if (!this.state.foreground || !this.state.connected || this.state.busy || this.state.buttonInFlight || this.state.touchInFlight) return;
    const epoch: number = this.state.generation;
    this.state.buttonInFlight = true;
    try {
      await companion.press(code);
      if (this.current(epoch)) this.state.status = '已连接 · 按键已发送';
    } catch (error) {
      if (!this.current(epoch)) return;
      companion.cancel();
      this.state.connected = false;
      this.state.status = this.errorMessage((error as CompanionError).code);
      // Never replay a failed input command; user must decide whether to press again.
    } finally {
      if (this.current(epoch)) this.state.buttonInFlight = false;
    }
  }

  beginTouch(x: number, y: number): boolean {
    if (!this.state.foreground || !this.state.connected || this.state.busy || this.state.buttonInFlight ||
      this.state.touchInFlight || !Number.isFinite(x) || !Number.isFinite(y)) return false;
    const epoch: number = this.state.generation;
    const pump: TouchGesturePump = new TouchGesturePump(
      (sample: TouchSample) => this.sendTouchSample(pump, epoch, sample),
      (error: Error) => this.failTouch(pump, epoch, error));
    this.touchPump = pump;
    this.touchHeld = false;
    this.state.touchInFlight = true;
    pump.offer(new TouchSample(x, y, TouchSample.BEGIN));
    return true;
  }

  moveTouch(x: number, y: number): void {
    if (!Number.isFinite(x) || !Number.isFinite(y)) { this.cancelTouch(); return; }
    this.touchPump?.offer(new TouchSample(x, y, TouchSample.MOVE));
  }

  endTouch(x: number, y: number, tap: boolean): void {
    if (!Number.isFinite(x) || !Number.isFinite(y)) { this.cancelTouch(); return; }
    this.touchPump?.offer(new TouchSample(x, y, TouchSample.END, tap));
  }

  holdTouch(): void {
    this.touchPump?.offer(new TouchSample(0, 0, TouchSample.HOLD));
  }

  /** Modal, mode, pointer, and watchdog cancellation: END/release, never SELECT. */
  cancelTouch(): void {
    this.touchPump?.cancel();
    this.clearTouchRelease();
  }

  private async sendTouchSample(pump: TouchGesturePump, epoch: number, sample: TouchSample): Promise<void> {
    if (!this.touchCurrent(pump, epoch)) return;
    if (sample.phase === TouchSample.HOLD) {
      await companion.holdSelect(true);
      if (this.touchCurrent(pump, epoch)) {
        this.touchHeld = true;
        // Relative timer begins only after native SELECT-down completes. Never
        // measure a hold with wall time, which can jump when clocks synchronize.
        this.touchHoldWait = this.waitTouchRelease(1000);
      }
      return;
    }
    await companion.touch(sample.x, sample.y, sample.phase);
    if (!this.touchCurrent(pump, epoch) || sample.phase !== TouchSample.END) return;
    // Match Android's real one-second SELECT-down minimum. Cancellation skips
    // the delay. The timer is cleared on every session invalidation.
    if (sample.cancelled) this.clearTouchRelease();
    else if (this.touchHeld && this.touchHoldWait !== undefined) await this.touchHoldWait;
    if (!this.touchCurrent(pump, epoch)) return;
    if (this.touchHeld) {
      await companion.holdSelect(false);
      if (!this.touchCurrent(pump, epoch)) return;
      this.touchHeld = false;
    }
    // END must finish before a tap SELECT. Cancellation mutates the same terminal
    // sample, including while the native END Promise or hold timer is pending.
    if (sample.tap && !sample.cancelled) {
      await companion.press(6);
      if (!this.touchCurrent(pump, epoch)) return;
    }
    this.touchPump = undefined;
    this.state.touchInFlight = false;
    this.state.status = sample.cancelled ? '已连接 · 触控已取消' : '已连接 · 触控已发送';
  }

  private touchCurrent(pump: TouchGesturePump, epoch: number): boolean {
    return this.touchPump === pump && this.current(epoch) && this.state.connected;
  }

  private failTouch(pump: TouchGesturePump, epoch: number, error: Error): void {
    if (!this.touchCurrent(pump, epoch)) return;
    this.abortTouch();
    companion.cancel();
    this.state.connected = false;
    this.state.status = this.errorMessage((error as CompanionError).code);
    // Input failures, including authentication failures, never replay gestures.
  }

  private waitTouchRelease(delay: number): Promise<void> {
    return new Promise<void>((resolve: () => void) => {
      this.touchReleaseResolve = resolve;
      this.touchReleaseTimer = setTimeout(() => {
        // A callback already dispatched before clearTimeout must not clear the
        // timer or Promise belonging to a newer gesture/session.
        if (this.touchReleaseResolve !== resolve) return;
        this.touchReleaseTimer = -1;
        this.touchReleaseResolve = undefined;
        this.touchHoldWait = undefined;
        resolve();
      }, delay);
    });
  }

  private clearTouchRelease(): void {
    if (this.touchReleaseTimer >= 0) { clearTimeout(this.touchReleaseTimer); this.touchReleaseTimer = -1; }
    const resolve: (() => void) | undefined = this.touchReleaseResolve;
    this.touchReleaseResolve = undefined;
    this.touchHoldWait = undefined;
    if (resolve !== undefined) resolve();
  }

  private abortTouch(): void {
    this.touchPump?.abort();
    this.touchPump = undefined;
    this.state.touchInFlight = false;
    this.touchHeld = false;
    this.clearTouchRelease();
  }

  disconnect(): void { this.stopSession(true); }

  async forgetSelected(expectedId: string): Promise<void> {
    const target: Device | undefined = this.state.selected;
    const store: CredentialStore | undefined = this.credentials;
    if (target === undefined || target.id !== expectedId || store === undefined || !this.state.foreground || this.state.busy) return;
    this.stopSession(true);
    const epoch: number = this.state.generation;
    this.state.busy = true;
    try {
      await store.remove(target.id);
      if (this.lastDevice?.id === target.id) {
        this.lastDevice = undefined;
        this.settings?.deleteSync('last_id');
        this.flushSettings();
      }
      if (this.current(epoch)) this.state.status = '本机配对身份已移除。电视上的已授权遥控器记录需在电视端另行移除。';
    } catch (_) {
      if (this.current(epoch)) this.state.status = '无法删除本机配对身份；请重试。';
    } finally {
      if (this.current(epoch)) { this.state.busy = false; this.refreshForget(); }
    }
  }

  private connected(target: Device): void {
    // Only called after the native verified/encrypted session startup Promise resolves.
    this.state.connected = true; this.state.selected = target; this.state.needsPin = false;
    this.state.status = '已连接 · 设备身份验证与加密会话成功';
    this.lastDevice = target; this.reconnectEnabled = true; this.reconnectAttempts = 2;
    try {
      this.settings?.putSync('last_id', target.id);
      this.settings?.putSync('last_name', target.name);
      this.settings?.putSync('last_host', target.host);
      this.settings?.putSync('last_port', target.port);
      this.settings?.putSync('resume_connection', true);
      this.flushSettings();
    } catch (_) { this.state.status = '已连接，但无法保存下次恢复连接的设置。'; }
    this.refreshForget();
  }

  private stopSession(userInitiated: boolean): void {
    this.clearReconnectTimer();
    this.abortTouch();
    ++this.state.generation;
    this.state.connected = false; this.state.busy = false; this.state.buttonInFlight = false;
    this.state.needsPin = false; this.state.pin = ''; this.state.status = '未连接';
    if (userInitiated) {
      this.reconnectEnabled = false;
      try { this.settings?.putSync('resume_connection', false); this.flushSettings(); }
      catch (_) { this.state.status = '已断开，但无法保存自动重连设置。'; }
    }
    companion.cancel();
    // Hard shutdown is deliberate for background/disconnect: native cancel()
    // closes transport and invalidates active/queued I/O synchronously. Remote
    // receipt of END/SELECT-up cannot be guaranteed after socket closure/loss.
    // In-session cancelTouch() instead sends END/SELECT-up before unlocking.
    companion.disconnect().catch(() => {});
  }

  private scheduleReconnect(): void {
    const target: Device | undefined = this.lastDevice;
    if (!this.state.foreground || !this.reconnectEnabled || this.reconnectAttempts >= 2 || target === undefined || this.reconnectTimer >= 0) return;
    if (!this.hasIdentity(target.id)) return;
    const epoch: number = this.state.generation;
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = -1;
      if (!this.current(epoch) || !this.reconnectEnabled || this.state.busy || this.state.connected || this.state.needsPin) return;
      ++this.reconnectAttempts;
      // Discovery may refresh an address/port only for the same stored service key.
      // That key is just a locator: the original saved public key remains the trust anchor.
      // Native Pair Verify must reject a different TV impersonating this mDNS name.
      const located: Device | undefined = this.state.devices.find((device: Device) => device.id === target.id);
      const endpoint: Device = located ?? target;
      this.state.selected = endpoint;
      this.connectDevice(endpoint, true);
    }, 3000);
  }

  private clearReconnectTimer(): void {
    if (this.reconnectTimer >= 0) { clearTimeout(this.reconnectTimer); this.reconnectTimer = -1; }
  }
  private current(epoch: number): boolean { return epoch === this.state.generation && this.state.foreground; }
  private refreshForget(): void {
    const id: string = this.state.selected?.id ?? '';
    this.state.canForget = id.length > 0 && this.hasIdentity(id);
  }
  private hasIdentity(id: string): boolean {
    try { return this.credentials?.contains(id) === true; }
    catch (_) {
      this.state.ready = false; this.state.connected = false; this.reconnectEnabled = false;
      this.abortTouch();
      this.clearReconnectTimer(); companion.cancel();
      this.state.status = '安全存储状态不确定，连接已停用。请重新打开应用后检查配对身份。';
      return false;
    }
  }
  private flushSettings(): void {
    this.settings?.flush().catch(() => {
      if (this.state.foreground) this.state.status = '无法保存连接偏好；本次连接状态不受影响。';
    });
  }
  private networkRetryable(code: string): boolean { return code === 'TIMEOUT' || code === 'DISCONNECTED'; }
  private errorMessage(code: string): string {
    switch (code) {
      case 'AUTH_FAILED': return '验证失败：PIN 错误或配对身份已失效。请核对电视；必要时忘记设备后重新配对。';
      case 'IDENTITY_CHANGED': return '设备身份与已保存的身份不同。已拒绝连接；请核对目标，确认后再忘记并重新配对。';
      case 'TIMEOUT': return '连接超时。请确认电视已唤醒、处于同一网络，且端口正确。';
      case 'DISCONNECTED': return '网络连接已中断。不会自动重发按键。';
      case 'PROTOCOL_ERROR': return '协议或加密响应无效。已关闭连接；此 POC 可能尚不兼容当前 tvOS。';
      case 'INVALID_INPUT': return '地址、端口、PIN 或保存的身份格式无效。';
      case 'NOT_CONNECTED': return '安全会话尚未建立，请重新连接。';
      case 'BUSY': return '上一项原生操作仍在清理，请稍后重试。';
      case 'CANCELLED': return '操作已取消。';
      default: return '原生操作失败，连接已关闭。请重试或检查构建与设备兼容性。';
    }
  }
}

export const remoteController: RemoteController = new RemoteController();
