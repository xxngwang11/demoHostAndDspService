@Entry
@Component
struct Index {
  build() {
    Column() {
      Text('DspService 正在运行')
        .fontSize(20)
        .fontWeight(FontWeight.Medium)
        .margin({ top: 40 })

      Text('请通过 HostApp 连接此服务')
        .fontSize(14)
        .fontColor('#666666')
        .margin({ top: 12 })
    }
    .width('100%')
    .height('100%')
    .justifyContent(FlexAlign.Center)
    .alignItems(HorizontalAlign.Center)
  }
}
