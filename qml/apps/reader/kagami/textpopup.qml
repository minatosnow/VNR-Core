/** popupmanager.qml
 *  8/16/2014 jichi
 *  Text popup manager for OCR.
 */
import QtQuick 1.1
import QtDesktop 0.1 as Desktop
import org.sakuradite.reader 1.0 as Plugin
import '../../../js/sakurakit.min.js' as Sk
import '../../../js/reader.min.js' as My
import '../../../js/util.min.js' as Util
import '../../../components' as Components
import '../../../js/local.js' as Local // Local.comet
import '../share' as Share

Item { id: root_

  property real zoomFactor: 1.0
  property bool ignoresFocus: false

  // - Private -

  Component.onCompleted: Local.items = [] // [item]

  Plugin.TextPopupBean { //id: bean_
    Component.onCompleted:
      popupRequested.connect(showPopup)
  }

  function showPopup(text, x, y) { // string ->
    var items = Local.items
    for (var i in items) {
      var item = items[i]
      if (!item.visible && !item.locked) {
        console.log("textpopup.qml:showPopup: reuse existing item")
        item.show(text, x, y)
        return
      }
    }
    console.log("textpopup.qml:showPopup: create new item")
    var item = comp_.createObject(root_)
    item.show(text, x, y)
    items.push(item)
  }

  // Component

  Component { id: comp_

    Rectangle { id: item_
      property int minimumX: root_.x
      property int minimumY: root_.y
      property int maximumX: minimumX + root_.width - width
      property int maximumY: minimumY + root_.height - height

      property alias text: textEdit_.text

      property bool locked: false // indicate whether this object is being translated

      function show(text, x, y) { // string, int, int ->
        reset()
        item_.text = text
        item_.x = x
        item_.y = y
        ensureVisible()
        visible = true
      }

      function hide() { visible = false }

      // - Private -

      function reset() {
        translateButton_.enabled = true
        toolTip_.text = qsTr("You can drag the border to move the text box")
      }

      Component.onCompleted: console.log("textpopup.qml:onCompleted: pass")
      Component.onDestruction: console.log("textpopup.qml:onDestruction: pass")

      radius: 10
      color: '#99000000' // black

      property int _CONTENT_MARGIN: 10

      width: scrollArea_.width + _CONTENT_MARGIN * 2
      height: scrollArea_.height + _CONTENT_MARGIN * 2

      property int _MAX_WIDTH: 200 * root_.zoomFactor
      property int _MAX_HEIGHT: 200 * root_.zoomFactor

      // So that the popup will not be out of screen
      function ensureVisible() {
        if (x < minimumX)
          x = minimumX
        if (y < minimumY)
          y = minimumY
        if (x > maximumX)
          x = maximumX
        if (y > maximumY)
          y = maximumY
      }

      function translate() {
        item_.locked = true
        var text = textEdit_.text
        if (text) {
          var keys = trPlugin_.translators().split(',')
          if (keys.length)
            textEdit_.textFormat = TextEdit.RichText
            for (var i in keys) {
              var key = keys[i]
              var tr = trPlugin_.translate(text, key)
              if (tr && tr != text)
                appendTranslation(tr, key)

              var name = My.tr(Util.translatorName(key))
              if (i == 0)
                toolTip_.text = name
              else
                toolTip_.text += ", " + name
            }
        }
        item_.locked = false
      }

      function appendTranslation(tr, key) { // translator key, translation text
        var text = textEdit_.text
        if (text)
          text += '<br/>'
        text += tr
        textEdit_.text = text
      }

      MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        drag {
          target: item_
          axis: Drag.XandYAxis

          minimumX: item_.minimumX; minimumY: item_.minimumY
          maximumX: item_.maximumX; maximumY: item_.maximumY
        }
      }

      Desktop.TooltipArea { id: toolTip_
        anchors.fill: parent
      }

      Flickable { id: scrollArea_
        anchors.centerIn: parent
        height: Math.min(_MAX_HEIGHT, textEdit_.paintedHeight)
        width: textEdit_.width

        //contentWidth: textEdit_.paintedWidth
        contentHeight: textEdit_.paintedHeight
        clip: true

        states: State {
          when: scrollArea_.movingVertically || scrollArea_.movingHorizontally
          PropertyChanges { target: verticalScrollBar_; opacity: 1 }
          //PropertyChanges { target: horizontalScrollBar_; opacity: 1 }
        }

        transitions: Transition {
          NumberAnimation { property: 'opacity'; duration: 400 }
        }

        TextEdit { id: textEdit_
          anchors.centerIn: parent
          width: _MAX_WIDTH // FIXME: automatically adjust width

          //selectByMouse: true // conflicts with flickable

          textFormat: TextEdit.PlainText
          wrapMode: TextEdit.Wrap
          focus: true
          color: 'snow'
          font.pixelSize: 12 * root_.zoomFactor
          //font.bold: true
          //font.family: 'MS Mincho' // 明朝

          // Not working, which cause textedit width to shrink
          //onTextChanged: width = Math.min(_MAX_WIDTH, paintedWidth)

          onLinkActivated: Qt.openUrlExternally(link)
          //console.log("shiori.qml: link activated:", link)

          effect: Share.TextEffect {}

          MouseArea { //id: textCursor_
            anchors.fill: parent
            //acceptedButtons: enabled ? Qt.LeftButton : Qt.NoButton
            acceptedButtons: Qt.LeftButton
            //enabled: !!model.text
            //hoverEnabled: enabled

            // Disabled since TTS does not work in admin
            //onDoubleClicked: {
            //  textEdit_.cursorPosition = textEdit_.positionAt(mouse.x, mouse.y)
            //  textEdit_.selectWord()
            //  var t = textEdit_.selectedText
            //  if (t) {
            //    clipboardPlugin_.text = t
            //    ttsPlugin_.speak(t, 'ja')
            //  }
            //}
          }
        }
      }

      Components.ScrollBar { id: verticalScrollBar_
        width: 12
        height: Math.max(0, scrollArea_.height - 12)
        anchors.right: scrollArea_.right
        anchors.verticalCenter: scrollArea_.verticalCenter
        opacity: 0
        orientation: Qt.Vertical
        position: scrollArea_.visibleArea.yPosition
        pageSize: scrollArea_.visibleArea.heightRatio
      }

      Desktop.ContextMenu { id: contextMenu_
        //property int popupX
        //property int popupY

        function popup(x, y) {
          //popupX = x; popupY = y
          showPopup(x, y)
        }

        Desktop.MenuItem { id: copyAct_
          text: Sk.tr("Copy")
          shortcut: "Ctrl+C"
          onTriggered: {
            textEdit_.selectAll()
            textEdit_.copy()
          }
        }

        Desktop.Separator {}

        Desktop.MenuItem {
          text: Sk.tr("Close")
          //shortcut: "Esc"
          onTriggered: item_.hide()
        }
      }

      MouseArea { id: mouse_
        anchors.fill: parent
        //hoverEnabled: true
        acceptedButtons: Qt.RightButton
        //onEntered: item_.show()
        //onExited: item_.show() // bypass restart timer issue
        onPressed: if (!root_.ignoresFocus) {
          //var gp = Util.itemGlobalPos(parent)
          var gp = mapToItem(null, x + mouse.x, y + mouse.y)
          contextMenu_.popup(gp.x, gp.y)
        }
      }

      Rectangle { id: header_
        anchors {
          left: parent.left
          bottom: parent.top
          //bottomMargin: -_MARGIN*2
        }
        radius: 7

        property int _MARGIN: 2
        width: headerRow_.width + _MARGIN * 2 + _MARGIN * 8
        height: headerRow_.height + _MARGIN * 2

        color: item_.color

        opacity: 0.01 // invisible by default

        property bool active: toolTip_.containsMouse
                           || headerTip_.containsMouse
                           || headerRow_.hover

        states: State { name: 'ACTIVE'
          when: header_.active
          PropertyChanges { target: header_; opacity: 0.8 }
          //PropertyChanges { target: horizontalScrollBar_; opacity: 1 }
        }

        transitions: Transition { from: 'ACTIVE'
          NumberAnimation { property: 'opacity'; duration: 400 }
        }

        MouseArea {
          anchors.fill: parent
          acceptedButtons: Qt.LeftButton
          drag {
            target: item_
            axis: Drag.XandYAxis

            minimumX: item_.minimumX; minimumY: item_.minimumY
            maximumX: item_.maximumX; maximumY: item_.maximumY
          }
        }

        Desktop.TooltipArea { id: headerTip_
          anchors.fill: parent
          text: qsTr("You can drag the border to move the text box")
        }

        Row { id: headerRow_
          //anchors.centerIn: parent
          anchors {
            verticalCenter: parent.verticalCenter
            left: parent.left
            leftMargin: header_._MARGIN
          }

          property bool hover: closeButton_.hover || translateButton_.hover

          spacing: header_._MARGIN * 2

          property int cellWidth: 15
          property int pixelSize: 10
          property color backgroundColor

          Share.CircleButton { id: closeButton_
            diameter: parent.cellWidth
            font.pixelSize: parent.pixelSize
            font.bold: hover
            font.family: 'MS Gothic'
            backgroundColor: 'transparent'

            text: "×" // ばつ
            toolTip: Sk.tr("Close")
            onClicked: item_.hide()
          }

          Share.CloseButton { id: translateButton_
            diameter: parent.cellWidth
            font.pixelSize: parent.pixelSize
            //font.bold: hover    // bold make the text too bold
            font.family: 'MS Gothic'
            backgroundColor: 'transparent'

            color: enabled ? 'snow' : 'gray'
            text: '訳'
            toolTip: Sk.tr("Translate")
            onClicked:
              if (enabled && textEdit_.text) {
                enabled = false
                toolTip_.text = qsTr('Translating') + '...'
                item_.translate()
              }
          }
        }
      }
    }
  }
}
