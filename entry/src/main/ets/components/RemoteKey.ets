// Vector icons and palette follow Android RemoteScreen.kt; commands stay in RemoteController.
@Component
export struct RemoteKey {
  @Prop label: string = '';
  @Prop icon: Resource = $r('app.media.remote_select');
  @Prop inputEnabled: boolean = false;
  @Prop available: boolean = false;
  @Prop primary: boolean = false;
  @Prop showLabel: boolean = false;
  @Prop keyWidth: number = 72;
  @Prop keyHeight: number = 72;
  onPress: () => void = () => {};

  build() {
    Button({ type: ButtonType.Normal }) {
      Column({ space: 6 }) {
        Image(this.icon).width(24).height(24)
          .opacity(this.available ? 1 : 0.45)
        if (this.showLabel) {
          Text(this.label).fontSize(12).fontColor(this.available ? '#E6EEEA' : '#71817C')
            .textAlign(TextAlign.Center).maxLines(2)
        }
      }.padding(8)
    }
    .width(this.keyWidth).height(this.keyHeight)
    .borderRadius(this.primary ? this.keyWidth / 2 : 20)
    .backgroundColor(this.available && this.primary ? '#B8E4CA' : (this.showLabel ? '#243133' : '#182224'))
    .opacity(1)
    .enabled(this.inputEnabled)
    .accessibilityText(this.label)
    .stateEffect(true)
    .onClick(() => { this.onPress(); })
  }
}
