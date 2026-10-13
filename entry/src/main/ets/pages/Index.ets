import { common } from '@kit.AbilityKit';
import { Device } from '../model/Device';
import { RemoteState } from '../model/RemoteState';
import { remoteController } from '../model/RemoteController';
import { RemoteLayout } from '../model/RemoteLayout';
import { RemoteKey } from '../components/RemoteKey';
import { RemoteTouchPad } from '../components/RemoteTouchPad';

@Entry
@Component
struct Index {
  @State state: RemoteState = remoteController.state;
  @State panel: string = '';
  @State touchMode: boolean = true;
  @State manualAddress: string = '';
  @State manualPort: string = '';
  @State forgetTargetId: string = '';
  @State forgetTargetName: string = '';
  @State windowWidth: number = 360;
  @State windowHeight: number = 640;
  @State fontScale: number = 1;

  aboutToAppear(): void {
    this.fontScale = (this.getUIContext().getHostContext() as common.UIAbilityContext | undefined)?.config.fontSizeScale ?? 1;
  }

  aboutToDisappear(): void { remoteController.cancelTouch(); }

  private setPanel(panel: string): void {
    remoteController.cancelTouch();
    this.panel = panel;
  }

  private setTouchMode(touch: boolean): void {
    if (this.touchMode === touch) return;
    remoteController.cancelTouch();
    this.touchMode = touch;
  }

  // A contact stays enabled while its own bounded input pump is draining.
  private touchEnabled(): boolean {
    return this.state.foreground && this.state.connected && !this.state.busy &&
      !this.state.buttonInFlight && !this.modalOpen();
  }

  private layout(): RemoteLayout {
    return new RemoteLayout(this.windowWidth, this.windowHeight, this.fontScale);
  }

  private modalOpen(): boolean {
    return this.state.foreground && (this.panel.length > 0 || this.state.needsPin);
  }

  private controlsEnabled(): boolean {
    return this.state.foreground && this.state.connected && !this.state.busy && !this.state.buttonInFlight && !this.state.touchInFlight;
  }

  // A command in flight locks input without flashing every key to a disconnected palette.
  private controlsAvailable(): boolean {
    return this.state.foreground && this.state.connected && !this.state.busy;
  }

  private canSelect(): boolean {
    return this.state.foreground && this.state.ready && !this.state.busy && !this.state.buttonInFlight && !this.state.touchInFlight;
  }

  private sideLayout(): boolean {
    return this.layout().side;
  }

  private contentWidth(): number {
    return this.layout().contentWidth;
  }

  private padSize(): number {
    return this.layout().padSize;
  }

  private closePanel(): void {
    this.panel = '';
    this.forgetTargetId = '';
    this.forgetTargetName = '';
    this.manualAddress = '';
    this.manualPort = '';
  }

  onBackPress(): boolean {
    if (this.state.needsPin) {
      remoteController.disconnect();
      this.closePanel();
      return true;
    }
    if (this.panel.length > 0) {
      this.closePanel();
      return true;
    }
    return false;
  }

  private openManual(): void {
    this.manualAddress = this.state.selected?.host ?? '';
    this.manualPort = this.state.selected?.port.toString() ?? '';
    this.setPanel('manual');
  }

  private selectDevice(device: Device): void {
    if (!this.canSelect()) return;
    remoteController.select(device);
    this.closePanel();
    remoteController.connectSelected();
  }

  @Builder
  header() {
    Row({ space: 8 }) {
      Button({ type: ButtonType.Normal }) {
        Row({ space: 10 }) {
          Image($r('app.media.remote_device')).width(24).height(24)
          Column({ space: 4 }) {
            Text(this.state.selected?.name ?? '选择 Apple TV')
              .fontSize(16).fontWeight(FontWeight.Medium).fontColor('#E6EEEA')
              .maxLines(2).textOverflow({ overflow: TextOverflow.Ellipsis }).width('100%')
            Text(this.state.connected ? '已连接 · 切换设备' : (this.state.busy ? '连接中…' : '选择或连接设备'))
              .fontSize(12).fontColor('#ABBDB7').width('100%')
          }.layoutWeight(1).alignItems(HorizontalAlign.Start)
        }.padding({ left: 14, right: 14, top: 10, bottom: 10 }).constraintSize({ minHeight: 68 })
      }.layoutWeight(1).borderRadius(20).backgroundColor('#243133')
        .enabled(this.canSelect()).accessibilityText('选择或切换 Apple TV')
        .onClick(() => { this.setPanel('devices'); })
      Button('连接', { type: ButtonType.Normal }).fontSize(14).fontColor('#B8E4CA')
        .backgroundColor(Color.Transparent).height(48).padding({ left: 12, right: 12 })
        .onClick(() => { this.setPanel('options'); })
    }.width('100%')
  }

  @Builder
  modeLabel() {
    Flex({ direction: FlexDirection.Row, wrap: FlexWrap.Wrap }) {
      Button('触摸板', { type: ButtonType.Normal }).fontSize(14).height(48)
        .fontColor(this.touchMode ? '#D6F6E2' : '#ABBDB7')
        .backgroundColor(this.touchMode ? '#284C3C' : Color.Transparent).borderRadius(14)
        .padding({ left: 20, right: 20 }).accessibilityText(this.touchMode ? '触摸板，已选中' : '切换触摸板')
        .onClick(() => { this.setTouchMode(true); })
      Button('方向键', { type: ButtonType.Normal }).fontSize(14).height(48)
        .fontColor(this.touchMode ? '#ABBDB7' : '#D6F6E2')
        .backgroundColor(this.touchMode ? Color.Transparent : '#284C3C').borderRadius(14)
        .padding({ left: 20, right: 20 }).accessibilityText(this.touchMode ? '切换方向键' : '方向键，已选中')
        .onClick(() => { this.setTouchMode(false); })
    }.width('100%')
  }

  @Builder
  navigationPad() {
    if (this.touchMode) {
      RemoteTouchPad({ inputEnabled: this.touchEnabled(), available: this.controlsAvailable(), padSize: this.padSize(),
        onBegin: (x: number, y: number): boolean => { return remoteController.beginTouch(x, y); },
        onMove: (x: number, y: number) => { remoteController.moveTouch(x, y); },
        onEnd: (x: number, y: number, tap: boolean) => { remoteController.endTouch(x, y, tap); },
        onHold: () => { remoteController.holdTouch(); },
        onCancel: () => { remoteController.cancelTouch(); } })
    } else {
      this.directionPad()
    }
  }

  @Builder
  directionKey(label: string, icon: Resource, code: number, primary: boolean) {
    RemoteKey({ label: label, icon: icon, primary: primary,
      inputEnabled: this.controlsEnabled(), available: this.controlsAvailable(),
      keyWidth: (this.padSize() - 36) / 3, keyHeight: (this.padSize() - 36) / 3,
      onPress: () => { remoteController.press(code); } })
  }

  @Builder
  directionPad() {
    if (this.padSize() < 180) {
      // Keep useful hit targets in very narrow split-screen windows.
      Column({ space: 8 }) {
        this.wideKey('向上', 1)
        this.wideKey('向左', 3)
        this.wideKey('确认', 6)
        this.wideKey('向右', 4)
        this.wideKey('向下', 2)
      }.width('100%')
    } else {
      Column({ space: 6 }) {
        Row() {
          this.directionKey('向上', $r('app.media.remote_up'), 1, false)
        }.width('100%').justifyContent(FlexAlign.Center)
        Row({ space: 6 }) {
          this.directionKey('向左', $r('app.media.remote_left'), 3, false)
          this.directionKey('确认', $r('app.media.remote_select'), 6, true)
          this.directionKey('向右', $r('app.media.remote_right'), 4, false)
        }
        Row() {
          this.directionKey('向下', $r('app.media.remote_down'), 2, false)
        }.width('100%').justifyContent(FlexAlign.Center)
      }.padding(12).width(this.padSize()).height(this.padSize())
        .backgroundColor('#243133').borderRadius(32)
    }
  }

  @Builder
  wideKey(label: string, code: number) {
    Button(label, { type: ButtonType.Normal }).width('100%').constraintSize({ minHeight: 56 })
      .backgroundColor('#243133').fontColor('#E6EEEA').borderRadius(18)
      .enabled(this.controlsEnabled()).onClick(() => { remoteController.press(code); })
  }

  @Builder
  actionKey(label: string, icon: Resource, code: number) {
    RemoteKey({ label: label, icon: icon, showLabel: true,
      inputEnabled: this.controlsEnabled(), available: this.controlsAvailable(),
      keyWidth: this.layout().actionWidth,
      keyHeight: Math.max(80, 46 + 30 * this.fontScale), onPress: () => { remoteController.press(code); } })
  }

  @Builder
  actions() {
    if (this.sideLayout()) {
      Column({ space: 8 }) {
        this.actionKey('返回', $r('app.media.remote_left'), 5)
        this.actionKey('主屏幕', $r('app.media.remote_home'), 7)
        this.actionKey('播放 / 暂停', $r('app.media.remote_play'), 14)
      }.width(120)
    } else {
      Flex({ direction: FlexDirection.Row, wrap: FlexWrap.Wrap, justifyContent: FlexAlign.SpaceBetween }) {
        this.actionKey('返回', $r('app.media.remote_left'), 5)
        this.actionKey('主屏幕', $r('app.media.remote_home'), 7)
        this.actionKey('播放 / 暂停', $r('app.media.remote_play'), 14)
      }.width('100%')
    }
  }

  @Builder
  connectionStatus() {
    Text(this.state.status).fontSize(12).fontColor(this.state.connected ? '#B8E4CA' : '#ABBDB7')
      .width('100%').textAlign(TextAlign.Center).padding({ top: 4, bottom: 4 })
  }

  @Builder
  mainContent() {
    Column({ space: 12 }) {
      if (this.sideLayout()) {
        this.header()
        Row({ space: 16 }) {
          Scroll() {
            Column({ space: 12 }) {
              this.modeLabel()
              this.navigationPad()
              this.connectionStatus()
            }.width('100%')
          }.layoutWeight(1).height('100%').align(Alignment.Top)
            .onGestureJudgeBegin(() => this.state.touchInFlight ? GestureJudgeResult.REJECT : GestureJudgeResult.CONTINUE)
          Scroll() { this.actions() }.width(120).height('100%').align(Alignment.Bottom)
        }.layoutWeight(1).width('100%')
          .onGestureJudgeBegin(() => this.state.touchInFlight ? GestureJudgeResult.REJECT : GestureJudgeResult.CONTINUE)
      } else if (this.layout().dock) {
        this.header()
        this.modeLabel()
        Scroll() {
          Column({ space: 12 }) {
            this.navigationPad()
            this.connectionStatus()
          }.width('100%')
        }.layoutWeight(1).width('100%').align(Alignment.Center)
          .onGestureJudgeBegin(() => this.state.touchInFlight ? GestureJudgeResult.REJECT : GestureJudgeResult.CONTINUE)
        this.actions()
      } else {
        Scroll() {
          Column({ space: 12 }) {
            this.header()
            this.modeLabel()
            this.navigationPad()
            this.connectionStatus()
            this.actions()
          }.width('100%')
        }.layoutWeight(1).width('100%')
          .onGestureJudgeBegin(() => this.state.touchInFlight ? GestureJudgeResult.REJECT : GestureJudgeResult.CONTINUE)
      }
    }.width(this.contentWidth()).height('100%').padding({ top: 12, bottom: 12 })
      .enabled(!this.modalOpen())
      .accessibilityLevel(this.modalOpen() ? 'no-hide-descendants' : 'auto')
  }

  @Builder
  panelButton(label: string, action: () => void, enabled: boolean, primary: boolean) {
    Button(label, { type: ButtonType.Normal }).width('100%').constraintSize({ minHeight: 48 })
      .padding({ left: 16, right: 16, top: 12, bottom: 12 }).borderRadius(16).fontSize(15)
      .fontColor(primary ? '#123529' : '#B8E4CA')
      .backgroundColor(primary ? '#B8E4CA' : '#243133').enabled(enabled).onClick(action)
  }

  @Builder
  panelText(value: string) {
    Text(value).fontSize(14).fontColor('#ABBDB7').width('100%')
  }

  @Builder
  optionsPanel() {
    this.panelText(this.state.status)
    this.panelText(`${this.state.devices.length} 台附近设备 · ${this.state.discoveryText}`)
    this.panelText('与 Apple TV 连接同一 Wi-Fi。打开应用会尝试恢复已配对设备；主动断开后不会自动重连。')
    this.panelButton('选择或切换设备', () => { this.setPanel('devices'); }, this.canSelect(), false)
    this.panelButton(this.state.connected ? '断开连接' : (this.state.busy ? '取消连接' : '连接 / 配对'), () => {
      this.closePanel();
      if (this.state.connected || this.state.busy) remoteController.disconnect();
      else if (this.state.selected === undefined) this.setPanel('devices');
      else remoteController.connectSelected();
    }, this.state.foreground && this.state.ready, true)
    if (this.state.canForget) {
      this.panelButton('移除已保存配对', () => {
        this.forgetTargetId = this.state.selected?.id ?? '';
        this.forgetTargetName = this.state.selected?.name ?? 'Apple TV';
        this.setPanel('forget');
      }, this.canSelect(), false)
    }
    this.panelButton('关闭', () => { this.closePanel(); }, true, false)
  }

  @Builder
  deviceRow(device: Device) {
    Button({ type: ButtonType.Normal }) {
      Column({ space: 4 }) {
        Text(device.name).fontSize(16).fontColor('#E6EEEA').width('100%').maxLines(2)
        Text(`${device.host}:${device.port}`).fontSize(12).fontColor('#ABBDB7').width('100%')
      }.padding(14).width('100%').alignItems(HorizontalAlign.Start)
    }.width('100%').borderRadius(16)
      .backgroundColor(this.state.selected?.id === device.id ? '#284C3C' : '#243133')
      .enabled(this.canSelect()).onClick(() => { this.selectDevice(device); })
  }

  @Builder
  devicesPanel() {
    this.panelText(this.state.discoveryText)
    this.panelText('Companion 广播也可能来自其他 Apple 设备，请核对名称，并在电视上核对配对码。')
    if (this.state.selected !== undefined) {
      this.panelText('当前设备')
      this.deviceRow(this.state.selected as Device)
    }
    if (this.state.devices.length === 0) {
      this.panelText('暂未发现附近设备。请确认电视已唤醒且处于同一网络，或手动填写地址和端口。')
    }
    ForEach(this.state.devices, (device: Device) => {
      if (device.id !== this.state.selected?.id) {
        this.deviceRow(device)
      }
    }, (device: Device) => device.id)
    this.panelButton('手动填写地址和端口', () => { this.openManual(); }, this.canSelect(), false)
    this.panelButton('重新发现', () => { remoteController.refreshDiscovery(); }, this.state.ready && this.state.foreground, false)
    this.panelButton('关闭', () => { this.closePanel(); }, true, false)
  }

  @Builder
  manualPanel() {
    this.panelText('填写已发现的 Apple TV 地址和实际 Companion 端口。端口可能在电视重启后变化，没有固定默认值。')
    TextInput({ text: this.manualAddress, placeholder: '数字 IPv4 / IPv6 地址' })
      .type(InputType.Normal).maxLength(64).height(52).fontColor('#E6EEEA')
      .placeholderColor('#ABBDB7').backgroundColor('#243133')
      .enabled(this.canSelect()).onChange((value: string) => { this.manualAddress = value; })
    TextInput({ text: this.manualPort, placeholder: 'Companion 端口（必填）' })
      .type(InputType.Number).maxLength(5).height(52).fontColor('#E6EEEA')
      .placeholderColor('#ABBDB7').backgroundColor('#243133')
      .enabled(this.canSelect()).onChange((value: string) => { this.manualPort = value; })
    this.panelText(this.state.status)
    this.panelButton('连接 / 配对', () => {
      if (remoteController.selectManual(this.manualAddress, this.manualPort)) {
        this.closePanel();
        remoteController.connectSelected();
      }
    }, this.canSelect() && this.manualAddress.length > 0 && this.manualPort.length > 0, true)
    this.panelButton('取消', () => { this.closePanel(); }, true, false)
  }

  @Builder
  pinPanel() {
    this.panelText('输入电视屏幕上显示的 4 位数字。配对码仅用于本次验证。')
    TextInput({ text: this.state.pin, placeholder: '4 位配对码' })
      .type(InputType.NUMBER_PASSWORD).enableAutoFill(false).maxLength(4).height(56)
      .fontSize(22).fontColor('#E6EEEA').placeholderColor('#ABBDB7').backgroundColor('#243133')
      .enabled(!this.state.busy).onChange((value: string) => { this.state.pin = value.replace(/[^0-9]/g, '').slice(0, 4); })
    this.panelText(this.state.status)
    this.panelButton(this.state.busy ? '验证中…' : '配对', () => { remoteController.finishPairing(); },
      !this.state.busy && this.state.pin.length === 4, true)
    this.panelButton('取消配对', () => { remoteController.disconnect(); this.closePanel(); }, true, false)
  }

  @Builder
  forgetPanel() {
    this.panelText(`将移除「${this.forgetTargetName}」在本机保存的配对身份。下次连接需要重新输入电视上的配对码。电视端授权记录不会自动删除。`)
    this.panelButton('移除配对', () => {
      const target: string = this.forgetTargetId;
      this.closePanel();
      remoteController.forgetSelected(target);
    }, this.canSelect() && this.state.selected?.id === this.forgetTargetId, true)
    this.panelButton('取消', () => { this.closePanel(); }, true, false)
  }

  private panelTitle(): string {
    if (this.state.needsPin) return '与 Apple TV 配对';
    if (this.panel === 'devices') return '附近的设备';
    if (this.panel === 'manual') return '手动连接 Apple TV';
    if (this.panel === 'forget') return '移除已保存配对？';
    return '连接与设备';
  }

  @Builder
  dialog() {
    Column() {
      Scroll() {
        Column({ space: 16 }) {
          Text(this.panelTitle()).fontSize(22).fontWeight(FontWeight.Medium).fontColor('#E6EEEA').width('100%')
          if (this.state.needsPin) {
            this.pinPanel()
          } else if (this.panel === 'devices') {
            this.devicesPanel()
          } else if (this.panel === 'manual') {
            this.manualPanel()
          } else if (this.panel === 'forget') {
            this.forgetPanel()
          } else {
            this.optionsPanel()
          }
        }.width('100%').padding(24).backgroundColor('#182224').borderRadius(28)
      }.width('100%').constraintSize({ maxHeight: Math.max(120, this.windowHeight - 32) })
        .scrollBar(BarState.Auto)
    }.width(Math.max(0, Math.min(560, this.windowWidth - 32)))
  }

  build() {
    Stack({ alignContent: Alignment.Center }) {
      // Extend only the backdrop, preserving safe hit targets around cutouts and gestures.
      Column().width('100%').height('100%').backgroundColor('#101719')
        .expandSafeArea([SafeAreaType.SYSTEM, SafeAreaType.CUTOUT], [SafeAreaEdge.TOP, SafeAreaEdge.BOTTOM])
        .hitTestBehavior(HitTestMode.None)
      this.mainContent()
      if (this.modalOpen()) {
        // A full overlay blocks hit testing on the remote; outside taps deliberately do not dismiss a PIN.
        Column().width('100%').height('100%').backgroundColor('#B3000000').onClick(() => {})
        this.dialog()
      }
    }.width('100%').height('100%').backgroundColor('#101719')
      .onAreaChange((oldArea: Area, newArea: Area) => {
        this.windowWidth = Number(newArea.width);
        this.windowHeight = Number(newArea.height);
        this.fontScale = (this.getUIContext().getHostContext() as common.UIAbilityContext | undefined)?.config.fontSizeScale ?? 1;
      })
  }
}
