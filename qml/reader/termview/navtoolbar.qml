/** searchtoolbar.qml
 *  2/20/2013 jichi
 */
import QtQuick 1.1
//import QtDesktop 0.1 as Desktop
import '../../../js/sakurakit.min.js' as Sk
import '../../bootstrap3' as Bootstrap
//import '../share' as Share

//Row { id: root_
Column { id: root_

  signal scrollTop
  signal scrollBottom

  // - Private -

  spacing: 2

  //property int cellWidth: 20
  //property int cellHeight: 18
  //property int pixelSize: 10

  Bootstrap.Button {
    //width: parent.cellWidth
    //font.pixelSize: parent.pixelSize
    styleClass: 'btn btn-default'
    text: "↑" // うえ
    toolTip: qsTr("Scroll to the top")
    onClicked: root_.scrollTop()
    font.bold: true
    font.family: 'YouYuan'
    zoom: 0.8
    width: height
  }

  Bootstrap.Button {
    //width: parent.cellWidth
    //font.pixelSize: parent.pixelSize
    styleClass: 'btn btn-default'
    text: "↓" // した
    toolTip: qsTr("Scroll to the bottom")
    onClicked: root_.scrollBottom()
    font.bold: true
    font.family: 'YouYuan'
    zoom: 0.8
    width: height
  }
}

// EOF

  /*
  Share.TextButton {
    //language: 'ja'
    width: parent.cellWidth; height: parent.cellHeight
    //shadowWidth: width + 15; shadowHeight: height + 15
    font.pixelSize: parent.pixelSize
    //font.bold: true
    //color: 'snow'
    color: 'black'
    //backgroundColor: hover ? '#333' : '#666'
    backgroundColor: hover ? '#fff' : '#adadad'
    font.family: 'MS Gothic'

    text: '上'
    toolTip: qsTr("Scroll to the top")

    onClicked: root_.scrollTop()
  }

  Share.TextButton {
    //language: 'ja'
    width: parent.cellWidth; height: parent.cellHeight
    //shadowWidth: width + 15; shadowHeight: height + 15
    font.pixelSize: parent.pixelSize
    //font.bold: true
    color: 'black'
    backgroundColor: hover ? '#fff' : '#adadad'
    font.family: 'MS Gothic'

    text: '下'
    toolTip: qsTr("Scroll to the bottom")

    onClicked: root_.scrollBottom()
  }
  */
