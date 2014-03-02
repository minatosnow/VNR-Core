# coding: utf8
# about.py
# 12/28/2013 jichi

from PySide.QtCore import Qt
from Qt5 import QtWidgets
from sakurakit import skqss
from sakurakit.skclass import Q_Q
from sakurakit.sktr import tr_
#from mytr import mytr_
import info

class _AboutDialog:
  def __init__(self, q):
    self._createUi(q)

  def _createUi(self, q):
    layout = QtWidgets.QVBoxLayout()

    self.versionLabel = QtWidgets.QLabel()
    #self.versionLabel.setWordWrap(True)

    import rc
    url = rc.image_path('logo-reader')
    img = '<img src="%s" width=60 height=60 />' % url
    imageLabel = QtWidgets.QLabel(img)

    import main
    m = main.manager()
    wikiButton = QtWidgets.QPushButton(tr_("Wiki"))
    wikiButton.setToolTip(tr_("Wiki"))
    skqss.class_(wikiButton, 'btn btn-default')
    wikiButton.clicked.connect(lambda: m.openWiki('VNR/Text Settings'))

    updateButton = QtWidgets.QPushButton(tr_("Update"))
    updateButton.setToolTip(tr_("Update"))
    skqss.class_(updateButton, 'btn btn-primary')
    updateButton.clicked.connect(m.checkUpdate)

    creditButton = QtWidgets.QPushButton(tr_("Credits"))
    creditButton.setToolTip(tr_("Credits"))
    skqss.class_(creditButton, 'btn btn-info')
    creditButton.clicked.connect(m.showCredits)

    #helpEdit = QtWidgets.QLabel()
    helpEdit = QtWidgets.QTextBrowser()
    skqss.class_(helpEdit, 'texture')
    helpEdit.setReadOnly(True)
    helpEdit.setOpenExternalLinks(True)
    helpEdit.setHtml(info.renderAppHelp())

    #labels = QtWidgets.QHBoxLayout()
    #labels.addWidget(self.versionLabel)
    #labels.addWidget(imageLabel)
    #layout.addLayout(labels)

    layout.addWidget(self.versionLabel)
    row = QtWidgets.QHBoxLayout()
    #row.addLayout(labels)
    row.addWidget(imageLabel)
    row.addWidget(self.versionLabel)
    row.addStretch()
    row.addWidget(updateButton)
    row.addWidget(creditButton)
    row.addWidget(wikiButton)
    layout.addLayout(row)

    layout.addWidget(helpEdit)

    q.setLayout(layout)

  def refresh(self):
    import config, i18n, settings
    t = config.VERSION_TIMESTAMP
    line1 = tr_("Version") + " " + i18n.timestamp2datetime(t)
    t = settings.global_().updateTime() or config.VERSION_TIMESTAMP
    line2 = tr_("Update") + " " + i18n.timestamp2datetime(t)
    msg = '\n'.join((line1, line2))
    self.versionLabel.setText(msg)

class AboutDialog(QtWidgets.QDialog):
  def __init__(self, parent=None):
    WINDOW_FLAGS = Qt.Dialog | Qt.WindowMinMaxButtonsHint
    super(AboutDialog, self).__init__(parent, WINDOW_FLAGS)
    skqss.class_(self, 'texture')
    self.__d = _AboutDialog(self)
    #self.setWindowTitle(tr_("About {0}").format(mytr_("Visual Novel Reader")))
    self.setWindowTitle(tr_("About {0}").format("Visual Novel Reader"))
    self.resize(450, 400)

  def setVisible(self, t):
    """@reimp"""
    if t and t != self.isVisible():
      self.__d.refresh()
    super(AboutDialog, self).setVisible(t)

# EOF
