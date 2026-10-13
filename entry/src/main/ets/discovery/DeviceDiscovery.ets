import { common } from '@kit.AbilityKit';
import { mdns } from '@kit.NetworkKit';
import { Device } from '../model/Device';

/** mDNS names/TXT records are untrusted hints, never authenticated device identities. */
export default class DeviceDiscovery {
  private context: common.UIAbilityContext;
  private update: (devices: Device[], message: string) => void;
  private service: mdns.DiscoveryService | undefined = undefined;
  private devices: Map<string, Device> = new Map<string, Device>();
  private present: Set<string> = new Set<string>();
  private pending: mdns.LocalServiceInfo[] = [];
  private resolving: boolean = false;
  private generation: number = 0;
  private timer: number = -1;

  constructor(context: common.UIAbilityContext, update: (devices: Device[], message: string) => void) {
    this.context = context; this.update = update;
  }

  start(): void {
    if (this.service !== undefined) return;
    const epoch: number = ++this.generation;
    this.devices.clear(); this.present.clear(); this.pending = []; this.resolving = false;
    this.update([], '正在查找同一 Wi-Fi 上的 Companion 设备…');
    try {
      const discovery: mdns.DiscoveryService = mdns.createDiscoveryService(this.context, '_companion-link._tcp');
      this.service = discovery;
      discovery.on('discoveryStart', (event: mdns.DiscoveryEventInfo) => {
        if (epoch !== this.generation) return;
        if (event.errorCode !== undefined && event.errorCode !== 0) {
          this.stop(); this.update([], '发现服务启动失败；请检查网络后重试，也可以手动连接。');
        }
      });
      discovery.on('serviceFound', (info: mdns.LocalServiceInfo) => {
        if (epoch !== this.generation || info.serviceName.length === 0 || info.serviceName.length > 240) return;
        if (this.present.has(info.serviceName)) return;
        // Bound unauthenticated LAN input; discovery is a convenience, not a trust source.
        if (this.present.size >= 64) return;
        this.present.add(info.serviceName);
        this.pending.push(info);
        this.resolveNext(epoch);
      });
      discovery.on('serviceLost', (info: mdns.LocalServiceInfo) => {
        if (epoch !== this.generation) return;
        this.present.delete(info.serviceName); this.devices.delete(info.serviceName);
        this.publish();
      });
      discovery.startSearchingMDNS();
      this.timer = setTimeout(() => {
        if (epoch === this.generation && this.devices.size === 0) {
          this.update([], '未发现设备。请唤醒 Apple TV 并确认同一 Wi-Fi；也可以填写地址和实际端口。');
        }
      }, 12000);
    } catch (_) {
      this.stop(); this.update([], '发现不可用；请检查 Wi-Fi 或手动填写地址和实际端口。');
    }
  }

  private async resolveNext(epoch: number): Promise<void> {
    if (epoch !== this.generation || this.resolving || this.pending.length === 0) return;
    const info: mdns.LocalServiceInfo | undefined = this.pending.shift();
    if (info === undefined) return;
    if (!this.present.has(info.serviceName)) { this.resolveNext(epoch); return; }
    this.resolving = true;
    try {
      const resolved: mdns.LocalServiceInfo = await mdns.resolveLocalService(this.context, info);
      if (epoch !== this.generation || !this.present.has(info.serviceName)) return;
      const host: string = resolved.host?.address ?? '';
      const port: number = resolved.port ?? 0;
      if (host.length > 0 && host.length <= 128 && port >= 1 && port <= 65535 && Number.isInteger(port)) {
        this.devices.set(info.serviceName, new Device(`mdns:${info.serviceName}`, info.serviceName, host, port));
        this.publish();
      } else {
        this.update(Array.from(this.devices.values()), '已发现广播，但地址或实际端口解析无效；可重新发现。');
      }
    } catch (_) {
      if (epoch === this.generation) {
        this.update(Array.from(this.devices.values()), '部分服务地址解析失败；可重新发现或手动连接。');
      }
    } finally {
      // A stale resolve must not reset the new generation's in-flight flag or drain its queue.
      if (epoch === this.generation) { this.resolving = false; this.resolveNext(epoch); }
    }
  }

  private publish(): void {
    const list: Device[] = Array.from(this.devices.values());
    list.sort((a: Device, b: Device) => a.name.localeCompare(b.name));
    this.update(list, list.length > 0 ? `发现 ${list.length} 台 Companion 设备；请核对电视名称。` : '暂无设备；请唤醒 Apple TV。');
  }

  stop(): void {
    ++this.generation;
    if (this.timer >= 0) { clearTimeout(this.timer); this.timer = -1; }
    this.pending = []; this.present.clear(); this.devices.clear(); this.resolving = false;
    const active: mdns.DiscoveryService | undefined = this.service;
    this.service = undefined;
    if (active === undefined) return;
    try { active.stopSearchingMDNS(); } catch (_) {}
    try { active.off('serviceFound'); } catch (_) {}
    try { active.off('serviceLost'); } catch (_) {}
    try { active.off('discoveryStart'); } catch (_) {}
    // resolveLocalService has no cancellation API. Generation invalidation ignores every late result.
  }
}
