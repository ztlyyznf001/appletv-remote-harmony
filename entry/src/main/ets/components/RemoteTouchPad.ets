import { TouchContact } from '../model/TouchContact';

@Component
export struct RemoteTouchPad {
  @Prop @Watch('availabilityChanged') inputEnabled: boolean = false;
  @Prop available: boolean = false;
  @Prop padSize: number = 280;
  @State contacting: boolean = false;
  @State held: boolean = false;
  onBegin: (x: number, y: number) => boolean = () => false;
  onMove: (x: number, y: number) => void = () => {};
  onEnd: (x: number, y: number, tap: boolean) => void = () => {};
  onHold: () => void = () => {};
  onCancel: () => void = () => {};
  private contact: TouchContact = new TouchContact();
  private areaWidth: number = 0;
  private areaHeight: number = 0;
  private gestureGeneration: number = 0;
  private holdTimer: number = -1;
  private cancelTimer: number = -1;

  private availabilityChanged(): void {
    if (!this.inputEnabled) this.cancelContact();
  }

  aboutToDisappear(): void { this.cancelContact(); }

  private clearTimers(): void {
    ++this.gestureGeneration;
    if (this.holdTimer !== -1) clearTimeout(this.holdTimer);
    if (this.cancelTimer !== -1) clearTimeout(this.cancelTimer);
    this.holdTimer = -1; this.cancelTimer = -1;
  }

  private cancelContact(): void {
    const active: boolean = this.contact.active;
    this.clearTimers(); this.contact.reset(); this.contacting = false; this.held = false;
    if (active) this.onCancel();
  }

  private handleTouch(event: TouchEvent): void {
    // Never call preventDefault: ArkUI supports that method only on Hyperlink.
    event.stopPropagation();
    if (!this.inputEnabled || event.type === TouchType.Cancel) { this.cancelContact(); return; }
    // A second finger cancels the whole contact; it must never become a tap or a new gesture.
    if (event.touches.length > 1 || (this.contact.active &&
      event.changedTouches.some((point: TouchObject) => point.id !== this.contact.pointerId))) {
      this.cancelContact(); return;
    }
    if (event.type === TouchType.Down) {
      if (this.contact.active) { this.cancelContact(); return; }
      if (event.changedTouches.length !== 1) return;
      const point: TouchObject = event.changedTouches[0];
      if (!this.contact.begin(point.id, point.x, point.y, this.areaWidth, this.areaHeight, event.timestamp)) return;
      if (!this.onBegin(this.contact.x, this.contact.y)) { this.contact.reset(); return; }
      this.clearTimers();
      const generation: number = this.gestureGeneration;
      this.contacting = true;
      this.holdTimer = setTimeout(() => {
        if (generation !== this.gestureGeneration) return;
        this.holdTimer = -1;
        if (this.inputEnabled && this.contact.hold()) { this.held = true; this.onHold(); }
      }, TouchContact.HOLD_MS);
      this.cancelTimer = setTimeout(() => {
        if (generation === this.gestureGeneration) this.cancelContact();
      }, TouchContact.MAX_MS);
      return;
    }
    if (!this.contact.active) return;
    // ArkUI coalesces intermediate samples within a frame. Track their maximum
    // excursion too, so a fast out-and-back swipe cannot become a tap/hold.
    for (const historical of event.getHistoricalPoints()) {
      if (historical.touchObject.id !== this.contact.pointerId ||
        !this.contact.update(historical.touchObject.x, historical.touchObject.y)) {
        this.cancelContact(); return;
      }
    }
    const point: TouchObject | undefined = event.changedTouches.find((value: TouchObject) => value.id === this.contact.pointerId);
    if (point === undefined || !this.contact.update(point.x, point.y)) { this.cancelContact(); return; }
    if (event.type === TouchType.Up) {
      const x: number = this.contact.x;
      const y: number = this.contact.y;
      const tap: boolean = this.contact.finish(event.timestamp);
      this.clearTimers(); this.contacting = false; this.held = false;
      this.onEnd(x, y, tap);
    } else if (event.type === TouchType.Move && this.contact.shouldMove(event.timestamp)) {
      this.onMove(this.contact.x, this.contact.y);
    }
  }

  build() {
    Column({ space: 10 }) {
      Text(this.held ? '长按选择' : '触摸板').fontSize(22).fontWeight(FontWeight.Medium)
        .fontColor(this.available ? '#B8E4CA' : '#71817C')
      Text(this.available ? '滑动移动 · 轻点确认\n长按选择' : '连接后即可使用')
        .fontSize(13).fontColor('#ABBDB7').textAlign(TextAlign.Center)
    }.justifyContent(FlexAlign.Center).padding(16)
      .width(this.padSize).height(Math.max(180, this.padSize)).borderRadius(32)
      .backgroundColor(this.contacting ? '#284C3C' : '#243133')
      .border({ width: this.contacting ? 2 : 1, color: this.contacting ? '#B8E4CA' : '#354942' })
      .accessibilityText('Apple TV 触摸板。滑动移动，轻点确认，长按选择。无障碍按键请切换方向键。')
      .onTouch((event: TouchEvent) => { this.handleTouch(event); })
      .onAreaChange((oldArea: Area, newArea: Area) => {
        const width: number = Number(newArea.width);
        const height: number = Number(newArea.height);
        if (width !== this.areaWidth || height !== this.areaHeight) this.cancelContact();
        this.areaWidth = width; this.areaHeight = height;
      })
  }
}
