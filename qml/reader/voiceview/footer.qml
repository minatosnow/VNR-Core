/** footer.qml
 *  4/14/2014 jichi
 */
import QtQuick 1.1
//import QtDesktop 0.1 as Desktop
import '../../../js/sakurakit.min.js' as Sk
//import org.sakuradite.reader 1.0 as Plugin

Item { id: root_

  // - Private -

  height: text_.height + 10

  Text { id: text_
    anchors {
      left: parent.left
      right: parent.right
      leftMargin: 10
      rightMargin: 10
      verticalCenter: parent.verticalCenter
    }
    text: Sk.tr("Note") + ": " + qsTr("VNR will read translations instead of game texts if TTS's language is different from game's") + '.'
    wrapMode: Text.WordWrap
    color: 'purple'
  }
}
