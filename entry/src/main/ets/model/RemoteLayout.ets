// Same breakpoints and dimensions as the Android reference, without platform dependencies.
export class RemoteLayout {
  side: boolean;
  dock: boolean;
  contentWidth: number;
  padSize: number;
  actionWidth: number;

  constructor(width: number, height: number, fontScale: number) {
    this.side = width >= 600 && width > height && fontScale < 1.5;
    const gutter: number = width < 360 ? 16 : 24;
    this.contentWidth = Math.max(0, Math.min(width - gutter * 2, this.side ? 1040 : 520));
    const controls: number = this.contentWidth - (this.side ? 136 : 0);
    this.padSize = Math.max(0, Math.min(340, controls, Math.max(180, height - (this.side ? 150 : 280))));
    this.dock = this.side ? height >= 260 : fontScale < 1.5 && height >= 380 + 110 * fontScale;
    this.actionWidth = this.side ? 120 : Math.max(72, Math.min(112, (this.contentWidth - 16) / 3));
  }
}
