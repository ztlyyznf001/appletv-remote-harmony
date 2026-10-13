import { Device } from '../model/Device';
import { RemoteState } from '../model/RemoteState';
import { remoteController } from '../model/RemoteController';

@Entry
@Component
struct Index {
  @State state: RemoteState = remoteController.state;
  @State manualAddress: string = '';
  @State manualPort: string = '';
  @State showManual: boolean = false;
  @State showForget: boolean = false;
  @State forgetTargetId: string = '';

  @Builder
  key(label: string, code: number, width: number) {
    Button(label)
      .width(width).height(58).fontSize(17)
      .backgroundColor('#294D66').fontColor('#FFFFFF')
      .enabled(this.state.foreground && this.state.connected && !this.state.busy && !this.state.buttonInFlight)
      .onClick(() => { remoteController.press(code); })
  }

  build() {
    Scroll() {
      Column({ space: 16 }) {
        Column({ space: 6 }) {
          Text('Apple TV 遥控').fontSize(28).fontWeight(FontWeight.Bold).fontColor('#172431')
          Text('HarmonyOS 原生验证版 · Companion').fontSize(13).fontColor('#546674')
          Text(this.state.selected?.name ?? '尚未选择设备').fontSize(18).fontWeight(FontWeight.Medium).maxLines(2)
          Text(this.state.status).fontSize(14).fontColor(this.state.connected ? '#17654E' : '#445565')
            .width('100%').textAlign(TextAlign.Center)
        }.width('100%')

        Column({ space: 10 }) {
          Text('附近设备').fontSize(18).fontWeight(FontWeight.Bold).alignSelf(ItemAlign.Start)
          Text(this.state.discoveryText).fontSize(13).fontColor('#546674').width('100%')
          Text('广播可能来自其他 Apple 设备。名称和地址不能证明身份，请在电视上核对 PIN。')
            .fontSize(12).fontColor('#546674').width('100%')
          ForEach(this.state.devices, (device: Device) => {
            Button() {
              Column({ space: 3 }) {
                Text(device.name).fontSize(16).fontColor('#172431').width('100%').maxLines(2)
                Text(`${device.host}:${device.port}`).fontSize(12).fontColor('#546674').width('100%')
              }.padding(12).width('100%')
            }.width('100%').borderRadius(12).backgroundColor(this.state.selected?.id === device.id ? '#D7E8F3' : '#EDF2F5')
              .enabled(this.state.ready && !this.state.busy)
              .onClick(() => { this.showForget = false; remoteController.select(device); })
          }, (device: Device) => device.id)
          Row({ space: 10 }) {
            Button('重新发现').onClick(() => { remoteController.refreshDiscovery(); }).enabled(this.state.ready)
            Button(this.showManual ? '收起地址' : '手动地址')
              .onClick(() => { this.showManual = !this.showManual; }).enabled(!this.state.busy)
          }
          if (this.showManual) {
            Column({ space: 8 }) {
              TextInput({ text: this.manualAddress, placeholder: '数字 IPv4 / IPv6 地址' })
                .type(InputType.Normal).maxLength(64)
                .onChange((value: string) => { this.manualAddress = value; })
              TextInput({ text: this.manualPort, placeholder: '实际 Companion TCP 端口（必填）' })
                .type(InputType.Number).maxLength(5)
                .onChange((value: string) => { this.manualPort = value; })
              Text('从已解析的广播或可信网络工具确认端口；它可能变化，没有固定默认值。')
                .fontSize(12).fontColor('#546674')
              Button('选择此地址').width('100%').enabled(this.state.ready && !this.state.busy)
                .onClick(() => {
                  this.showForget = false;
                  if (remoteController.selectManual(this.manualAddress, this.manualPort)) this.showManual = false;
                })
            }.padding(12).backgroundColor('#EDF2F5').borderRadius(12)
          }
        }.width('100%').padding(16).backgroundColor('#FFFFFF').borderRadius(20)

        if (this.state.needsPin) {
          Column({ space: 10 }) {
            Text('输入电视上的 4 位 PIN').fontSize(18).fontWeight(FontWeight.Bold)
            TextInput({ text: this.state.pin, placeholder: '4 位 PIN' })
              .type(InputType.NUMBER_PASSWORD).enableAutoFill(false).maxLength(4).enabled(!this.state.busy)
              .onChange((value: string) => { this.state.pin = value.replace(/[^0-9]/g, '').slice(0, 4); })
            Button('验证并加密保存').enabled(!this.state.busy && this.state.pin.length === 4)
              .onClick(() => { remoteController.finishPairing(); })
            Button('取消配对').onClick(() => { remoteController.disconnect(); })
          }.width('100%').padding(16).backgroundColor('#FFFFFF').borderRadius(20)
        } else {
          Row({ space: 12 }) {
            Button(this.state.busy ? '正在连接…' : '连接 / 配对')
              .enabled(this.state.ready && this.state.selected !== undefined && !this.state.connected && !this.state.busy)
              .onClick(() => { remoteController.connectSelected(); })
            Button('断开').enabled(this.state.ready)
              .onClick(() => { remoteController.disconnect(); })
          }
        }

        Column({ space: 10 }) {
          this.key('上', 1, 80)
          Row({ space: 12 }) {
            this.key('左', 3, 80)
            this.key('确认', 6, 80)
            this.key('右', 4, 80)
          }
          this.key('下', 2, 80)
          Row({ space: 10 }) {
            this.key('返回', 5, 80)
            this.key('主屏幕', 7, 80)
            this.key('播放/暂停', 14, 96)
          }.margin({ top: 8 })
        }.width('100%').padding({ top: 18, bottom: 18 }).backgroundColor('#FFFFFF').borderRadius(20)

        if (this.state.canForget) {
          Button('忘记此设备').fontColor('#A63335').backgroundColor('#F5E8E9').enabled(!this.state.busy)
            .onClick(() => { this.forgetTargetId = this.state.selected?.id ?? ''; this.showForget = true; })
        }
        if (this.showForget) {
          Column({ space: 10 }) {
            Text('确认移除本机保存的配对身份？下次需要重新输入电视 PIN。电视端授权记录不会自动删除。')
              .fontSize(14).fontColor('#172431')
            Row({ space: 12 }) {
              Button('取消').onClick(() => { this.showForget = false; })
              Button('确认忘记').backgroundColor('#A63335').enabled(!this.state.busy)
                .onClick(() => {
                  this.showForget = false;
                  remoteController.forgetSelected(this.forgetTargetId);
                  this.forgetTargetId = '';
                })
            }
          }.width('100%').padding(16).backgroundColor('#FFFFFF').borderRadius(16)
        }
        Text('返回后台会关闭网络会话。成功连接后，下次进入前台最多尝试恢复两次；手动断开会关闭自动恢复。')
          .fontSize(12).fontColor('#546674').width('100%')
        Text('开发验证版；尚需真实 HarmonyOS 设备与 Apple TV 联调。').fontSize(12).fontColor('#546674')
      }.width('100%').constraintSize({ maxWidth: 520 }).padding(20)
    }.width('100%').height('100%').backgroundColor('#F2F5F7')
  }
}
