# coding: utf8
# rpcman.py
# 2/1/2013 jichi

__all__ = ['RpcServer', 'RpcClient']

from functools import partial
import json
from PySide.QtCore import Signal, Qt, QObject
from sakurakit.skclass import Q_Q, memoized
from sakurakit.skdebug import dwarn
import pyreader
from mytr import my
import config, growl

# Server

class RpcSignals(pyreader.ReaderMetaCallPropagator):

  activated = Signal()
  q_activated = Signal()

class RpcClient(QObject):

  def __init__(self, parent=None):
    super(RpcClient, self).__init__(parent)
    self.__d = d = RpcSignals(self)
    d.q_activated.connect(d.activated, Qt.QueuedConnection)

  def activate(self): self.__d.q_activated.emit()

  def stop(self): self.__d.stop()
  def isActive(self): return self.__d.isActive()
  def start(self): return self.__d.startClient('127.0.0.1', config.QT_METACALL_PORT)
  def waitForReady(self): self.__d.waitForReady()

@Q_Q
class _RpcServer(object):
  def __init__(self, q):
    self.r = r = RpcSignals(q)

    r.q_pingClient.connect(r.pingClient, Qt.QueuedConnection)
    r.q_callClient.connect(r.callClient, Qt.QueuedConnection)
    r.q_updateClientData.connect(r.updateClientData, Qt.QueuedConnection)

    r.growlServerMessage.connect(growl.msg)
    r.growlServerWarning.connect(growl.warn)
    r.growlServerError.connect(growl.error)

    r.pingServer.connect(partial(growl.msg,
      my.tr("Window text translator is loaded")
    ))
    r.pingServer.connect(q.connected)

    r.activated.connect(q.activated)
    r.updateServerData.connect(self._onDataReceived)

  def _onDataReceived(self, data):
    """
    @param  data  unicode
    """
    try:
      d = json.loads(data)
      if d and type(d) == dict:
        d = {long(k):v for k,v in d.iteritems()}
        self.q.textsReceived.emit(d)
      else:
        dwarn("error: json is not a map: %s" % data)
    except ValueError, e:
      dwarn(e)
      dwarn("error: malformed json: %s" % data)

class RpcServer(QObject):

  def __init__(self, parent=None):
    super(RpcServer, self).__init__(parent)
    self.__d =_RpcServer(self)

  activated = Signal()

  def stop(self):
    self.__d.r.stop()
  def start(self):
    """@return  bool"""
    return self.__d.r.startServer('127.0.0.1', config.QT_METACALL_PORT)
  def isActive(self):
    """@return  bool"""
    return self.__d.r.isActive()

  connected = Signal()
  textsReceived = Signal(dict) # [long hash:unicode text]

  def sendTranslation(self, data):
    """
    @param  data  {hash:translation}
    """
    try:
      data = json.dumps(data) #, ensure_ascii=False)
      self.__d.r.q_updateClientData.emit(data)
    except TypeError, e:
      dwarn("failed to encode json: %s" % e)

  def clearTranslation(self):
    self.__d.r.q_callClient.emit('clear')

  def enableClient(self):
    self.__d.r.q_callClient.emit('enable')

  def disableClient(self):
    self.__d.r.q_callClient.emit('disable')

@memoized
def manager(): return RpcServer()

# EOF
