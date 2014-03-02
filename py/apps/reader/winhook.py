# coding: utf8
# winhook.py
# 2/3/2013 jichi
# Windows only

from sakurakit import skos, skpaths, skwin, skwinsec
if skos.WIN:
  import os
  import config
  from sakurakit.skdebug import dprint

  def inject_process(pid):
    """
    @param  pid  ulong
    @return  bool
    """
    dprint("enter: pid = %s" % pid)
    ret = True
    for dllpath in config.WINHOOK_DLLS:
      #dllpath = os.path.abspath(dllpath)
      dllpath = skpaths.abspath(dllpath)
      assert os.path.exists(dllpath), "needed dll does not exist"
      ret = skwinsec.injectdll(dllpath, pid=pid) and ret
    dprint("leave: ret = %s" % ret)
    return ret

# EOF
