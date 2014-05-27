/** intspinbox.qml
 *  5/25/2014 jichi
 */
import QtQuick 1.1
import QtDesktop 0.1 as Desktop

Desktop.SpinBox {
  property int intValue

  // - Private -

  onIntValueChanged:
    value = intValue
  onValueChanged:
    if (parseInt(value) != intValue)
      intValue = value
}