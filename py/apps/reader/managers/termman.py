# coding: utf8
# termman.py
# 10/8/2012 jichi
#
# Terminology:
# - data: raw game text byte code
# - text: original game text
# - translation: machine translation
# - comment: user's subtitle or comment

import os, re
from functools import partial
from time import time
from PySide.QtCore import Signal, QObject, QTimer, QMutex
from zhszht.zhszht import zhs2zht
from sakurakit import skfileio, skthreads
from sakurakit.skclass import memoized, Q_Q
from sakurakit.skdebug import dprint, dwarn
from pytrscript import TranslationScriptManager
import config, dataman, defs, i18n, rc

@memoized
def manager(): return TermManager()

def _mark_text(text): # unicode -> unicode
  return '<span style="text-decoration:underline">%s</span>' % text

_re_marks = re.compile(r'<[0-9a-zA-Z: "/:=-]+?>')
def _remove_marks(text): return _re_marks.sub('', text)

RE_MACRO = re.compile('{{(.+?)}}')

class TermWriter:

  def __init__(self, createTime, terms, gameIds, hentai, marked):
    self.createTime = createTime # float
    self.terms = terms # [Term]
    self.gameIds = gameIds # set(ing gameId)
    self.hentai = hentai # bool
    self.marked = marked # bool

    #macros = self._queryTermMacros(terms, lang, gameIds, hentai) # {unicode pattern:unicode repl}

  def isOutdated(self): # -> bool
    return self.createTime < _TermManager.instance.updateTime

  def saveTerms(self, path, type, language):
    """This method is invoked from a different thread
    @param  path  unicode
    @param  type  str  term type
    @param  language  str  target text language
    @return  bool
    """
    marksChanges = self.marked and type in ('target', 'escape_before', 'escape_after') # bool
    convertsChinese = language == 'zht' and type in ('target', 'escape_before', 'escape_after') # bool

    try:
      empty = True
      with open(path, 'w') as f:
        f.write(self._createHeader(type, language, marksChanges))
        for td in self._iterTermData(type, language):
          if self.isOutdated():
            raise Exception("cancel saving out-of-date terms")
          z = convertsChinese and td.language == 'zhs'
          repl = td.text
          if repl:
            if z:
              repl = zhs2zht(repl)
            #elif config.is_latin_language(td.language):
            #  repl += " "
            if marksChanges:
              repl = _mark_text(repl)
          regex = td.regex
          pattern = td.pattern
          if z:
            pattern = zhs2zht(pattern)
          f.write(
              (("r\t%s\t%s\n" if regex else "\t%s\t%s\n") % (pattern, repl)) if repl else
              (("r\t%s\n" if regex else "\t%s\n") % pattern))
          empty = False

      if not empty:
        return True

    except Exception, e:
      dwarn(e)

    skfileio.removefile(path) # Remove file when failed
    return False

  def _createHeader(self, type, language, marked):
    """
    @param  type  str
    @param  language  str
    @param  marked  bool
    @return  unicode
    """
    return """\
# This file is automatically generated for debugging purposes.
# Modifying this file will NOT affect VNR.
#
# Unix time: %s
# Options: type = %s, language = %s, hentai = %s, underline = %s, games = (%s)
#
""" % (self.createTime, type, language, self.hentai, marked,
    ','.join(self.gameIds) if self.gameIds else 'empty')

  def _iterTermData(self, type, language):
    """
    @param  terms  [Term]
    @param  type  str
    @param  language  str
    @param  gameIds  set(int gameId)
    @param  hentai  bool
    @yield  _Term
    """
    type2 = 'name' if type == 'source' and not config.is_asian_language(language) else ''
    for t in self.terms:
      td = t.d # To improve performance
      if (not td.disabled and not td.deleted and td.pattern # in case pattern is deleted
          and (td.type == type or type2 and td.type == type2)
          and (not td.special or self.gameIds and td.gameId and td.gameId in self.gameIds)
          and (not td.hentai or self.hentai)
          and i18n.language_compatible_to(td.language, language)
        ):
        yield td

  def _queryTermMacros(self, language):
    """
    @param  terms  [Term]
    @param  language  str
    @param  gameIds  set(int gameId)
    @param  hentai  bool
    @return  {unicode pattern:unicode repl} not None
    """
    ret = {td.pattern:td.text  for td in self._iterTermData('macro', language)}
    MAX_COUNT = 1000
    for count in xrange(1, MAX_COUNT):
      dirty = False
      for pattern,text in ret.iteritems(): # not iteritems as I will modify ret
        if text and defs.TERM_MACRO_BEGIN in text:
          dirty = True
          ok = False
          for m in RE_MACRO.finditer(text):
            macro = m.group(1)
            repl = ret.get(macro)
            if repl:
              text = text.replace("{{%s}}" % macro, repl)
              ok = True
            else:
              dwarn("missing macro", macro, text)
              ok = False
              break
          if ok:
            ret[pattern] = text
          else:
            ret[pattern] = None # delete this pattern
      if not dirty:
        break
    if count == MAX_COUNT - 1:
      dwarn("recursive macro definition")
    return {k:v for k,v in ret.iteritems() if v is not None}

@Q_Q
class _TermManager:

  instance = None # _TermManager  needed for updateTime

  # Cover all term types, but decouple escape into before and after
  SAVE_TYPES = 'origin', 'source', 'target', 'speech', 'ocr'
  #SAVE_TYPES = 'escape_before', 'escape_after'

  def __init__(self, q):
    _TermManager.instance = self

    #self.convertsChinese = False
    self.enabled = True # bool
    self.hentai = False # bool

    self.marked = False # bool

    # For saving terms
    self.updateTime = 0 # float

    self.targetLanguage = 'ja' # str  targetLanguage
    self.targetLanguages = {} # [str language:float time]

    self.saveMutex = QMutex()

    self.scripts = {} # {unicode key:TranslationScriptManager}  key = lang + type

    t = self.saveTimer = QTimer(q)
    t.setSingleShot(True)
    t.setInterval(1000) # wait for 1 seconds for rebuilding
    t.timeout.connect(self.saveTerms)

  def rebuildCacheLater(self): self.saveTimer.start()

  def _createScriptManager(self, type, language): # unicode, unicode -> TranslationScriptManager
    key = type + language
    ret = self.scripts.get(key)
    if not ret:
      ret = self.scripts[key] = TranslationScriptManager()
    return ret

  def getScriptManager(self, type, language): # unicode, unicode -> TranslationScriptManager
    return self.scripts.get(type + language)

  #@classmethod
  #def needsEscape(cls):
  #  return config.is_asian_language(cls.language)

  def saveTerms(self):
    if not self.saveMutex.tryLock():
      dwarn("retry later due to thread contention")
      self.rebuildCacheLater()
      return

    saveTime = time()
    skthreads.runsync(partial(
        self._saveTerms, saveTime),
        parent=self.q)
    self.saveMutex.unlock()

  def _saveTerms(self, createTime):
    """Invoked async
    @param  createTime  float
    """
    dprint("enter")

    dm = dataman.manager()

    gameIds = dm.currentGameIds()
    if gameIds:
      gameIds = set(gameIds) # in case it is changed during iteration

    w = TermWriter(
      createTime=createTime,
      terms=dm.sortedTerms(), # not back up to save memory
      gameIds=gameIds,
      hentai=self.hentai,
      marked=self.marked,
    )

    langs = self.targetLanguages
    #for lang,ts in self.targetLanguages.iteritems():

    for lang,ts in langs.items(): # back up items
      if ts < self.updateTime: # skip language that does not out of date
        for type in self.SAVE_TYPES:
          if w.isOutdated():
            dwarn("leave: cancel saving out-of-date terms")
            return

          path = rc.term_path(type, lang) # unicode
          dir = os.path.dirname(path) # unicode path
          if not os.path.exists(dir):
            skfileio.makedirs(dir)

          man = self._createScriptManager(type, lang)
          if not man.isEmpty():
            man.clear()

          if w.saveTerms(path, type, lang) and man.loadFile(path):
            dprint("type = %s, lang = %s, count = %s" % (type, lang, man.size()))

        langs[lang] = createTime

    dprint("leave")

  def applyTerms(self, text, type, language):
    """
    @param  text  unicode
    @param  type  str
    @param  language  str
    """
    # TODO: Schedule to update terms when man is missing
    man = self.getScriptManager(type, language)
    if man is None:
      self.targetLanguages[language] = 0
      self.rebuildCacheLater()
    return man.translate(text) if man and not man.isEmpty() else text

class TermManager(QObject):

  ## Construction ##

  def __init__(self, parent=None):
    super(TermManager, self).__init__(parent)
    self.__d = _TermManager(self)

  cacheChanged = Signal()

  ## Properties ##

  #def isLocked(self): return self.__d.locked

  def setTargetLanguage(self, v):
    if v:
      self.__d.targetLanguage = v
      self.__d.targetLanguages = {v:0} # reset languages

  def isEnabled(self): return self.__d.enabled
  def setEnabled(self, value): self.__d.enabled = value

  def isHentaiEnabled(self): return self.__d.hentai
  def setHentaiEnabled(self, value):
    dprint(value)
    self.__d.hentai = value

  def isMarked(self): return self.__d.marked
  def setMarked(self, t): self.__d.marked = t

  ## Marks ##

  def clearMarkCache(self): # invoked on escapeMarked changes in settings
    for term in dataman.manager().iterEscapeTerms():
      term.applyReplace = None

  def markEscapeText(self, text): # unicode -> unicode
    return _mark_text(text) if text and self.__d.marked else text

  def removeMarks(self, text): # unicode -> unicode
    return _remove_marks(text) if self.__d.marked else text

  #def convertsChinese(self): return self.__d.convertsChinese
  #def setConvertsChinese(self, value): self.__d.convertsChinese = value

  ## Cache ##

  def invalidateCache(self):
    d = self.__d
    d.updateTime = time()
    d.rebuildCacheLater()

  warmup = invalidateCache

  #def warmup(self, async=True, interval=0): # bool, int
  #  d = self.__d
  #  if not d.enabled or d.locked:
  #    return
  #  dprint("enter")
  #  dm = dataman.manager()

  #  task = partial(d.warmup,
  #      terms=dm.terms(),
  #      hasTitles=dm.hasTermTitles(),
  #      hentai=d.hentai,
  #      language=d.language)

  #  if not async:
  #    apply(task)
  #  else:
  #    d.locked = True
  #    if interval:
  #      skthreads.runasynclater(task, interval)
  #    else:
  #      skthreads.runasync(task)
  #  dprint("leave")

  ## Queries ##

  #def filterTerms(self, terms, language):
  #  """
  #  @param  terms  iterable dataman.Term
  #  @param  language  unicode
  #  @yield  Term
  #  """
  #  return self.__d.iterTerms(terms, language)

  def applyTargetTerms(self, text, language):
    """
    @param  text  unicode
    @param  language  unicode
    @return  unicode
    """
    return self.__d.applyTerms(dataman.manager().iterTargetTerms(),
        text, language, convertsChinese=True, marksChanges=self.__d.marked)

  def applyOriginTerms(self, text, language):
    """
    @param  text  unicode
    @param  language  unicode
    @return  unicode
    """
    return self.__d.applyTerms(dataman.manager().iterOriginTerms(), text, language)

  #def applyNameTerms(self, text, language):
  #  """
  #  @param  text  unicode
  #  @param  language  unicode
  #  @return  unicode
  #  """
  #  return self.__d.applyTerms(dataman.manager().iterNameTerms(), text, language)

  def applySpeechTerms(self, text, language=None):
    """
    @param  text  unicode
    @param  language  unicode
    @return  unicode
    """
    return self.__d.applyTerms(text, 'speech', language or self.__d.targetLanguage)

  def applyOcrTerms(self, text, language=None):
    """
    @param  text  unicode
    @param  language  unicode
    @return  unicode
    """
    return self.__d.applyTerms(text, 'ocr', language or self.__d.targetLanguage)

  def applySourceTerms(self, text, language):
    """
    @param  text  unicode
    @param  language  unicode
    @return  unicode
    """
    dm = dataman.manager()
    d = self.__d
    text = d.applyTerms(dm.iterSourceTerms(), text, language)
    #if text and dm.hasNameItems() and config.is_latin_language(d.targetLanguage):
    #  try:
    #    for name in dm.iterNameItems():
    #      if name.translation:
    #        text = name.replace(text)
    #  except Exception, e: dwarn(e)
    #  text = text.rstrip() # remove trailing spaces
    return text

  def applyMacroTerms(self, text):
    """
    @param  text  unicode
    @return  unicode
    """
    d = self.__d
    if not d.enabled: #or d.locked: # disabling lock will cause terms cannot be init property on the startup
      return text
    dm = dataman.manager()
    # {{name}}
    for m in RE_MACRO.finditer(text):
      macro = m.group(1)
      repl = dm.queryTermMacro(macro)
      if repl is None:
        dwarn("missing macro", macro)
      else:
        text = text.replace("{{%s}}" % macro, repl)
    return text

  # Escaped

  def prepareEscapeTerms(self, text, language):
    """
    @param  text  unicode
    @param  language  unicode
    @return  unicode
    """
    d = self.__d
    if not d.enabled or d.locked:
      return text
    dm = dataman.manager()
    hasTitles = dm.hasTermTitles() # cached
    esc = defs.TERM_ESCAPE + ' '
    for term in dm.iterEscapeTerms():
      td = term.d # To improve performance
      if (not td.hentai or d.hentai) and td.pattern and i18n.language_compatible_to(td.language, language):
        if hasTitles and term.needsReplace():
          try: text = term.prepareReplace(text)
          except Exception, e: dwarn(td.pattern, td.text, e)
        else:
          h = term.priority or td.id or id(term)
          key = esc % h
          if not term.patternNeedsRe():
            text = text.replace(td.pattern, key)
          else:
            try: text = term.patternRe.sub(key, text)
            except Exception, e: dwarn(td.pattern, td.text, e)
        if not text:
          break
    #if text and dm.hasNameItems() and config.is_asian_language(d.targetLanguage):
    #  try:
    #    for name in dm.iterNameItems():
    #      if name.translation:
    #        text = name.prepareReplace(text)
    #  except Exception, e: dwarn(e)
    return text

  def applyEscapeTerms(self, text, language):
    """
    @param  text  unicode
    @param  language  unicode
    @return  unicode
    """
    d = self.__d
    if not d.enabled or d.locked:
      return text
    dm = dataman.manager()
    hasTitles = dm.hasTermTitles() # cached
    esc = defs.TERM_ESCAPE
    zht = language == 'zht' # cached
    for term in dm.iterEscapeTerms():
      td = term.d # To improve performance
      if (not td.hentai or d.hentai) and td.pattern and i18n.language_compatible_to(td.language, language):
        if hasTitles and term.needsReplace():
          try: text = term.applyReplace(text)
          except Exception, e: dwarn(td.pattern, td.text, e)
        else:
          #repl = term.bbcodeText if term.bbcode else term.text
          repl = td.text
          if repl:
            if zht and td.language == 'zhs':
              repl = zhs2zht(repl)
            if d.marked:
              repl = _mark_text(repl)
          #elif config.is_latin_language(td.language):
          #  repl += " "
          h = term.priority or td.id or id(term)
          key = esc % h
          text = text.replace(key, repl)
        if not text:
          break
    #if text and dm.hasNameItems() and config.is_asian_language(d.targetLanguage):
    #  try:
    #    for name in dm.iterNameItems():
    #      if name.translation:
    #        text = name.applyReplace(text)
    #  except Exception, e: dwarn(e)
    return text

# EOF

#  ## MeCab ##
#
#  def applyWordTerms(self, text):
#    """
#    @param  text  unicode
#    @return  unicode
#    """
#    # This feature is disabled
#    # Compiled MeCab dictionary id preferred
#    return text
#
#   #d = self.__d
#   #if not d.enabled or d.locked:
#   #  return text
#   #dm = dataman.manager()
#   #for term in dm.iterWordTerms():
#   #  td = term.d
#   #  if (not td.hentai or d.hentai) and not td.regex: #and td.language == 'ja': # skip using regular expressions, ignore languages
#   #    text = text.replace(td.pattern, " %s " % td.pattern)
#   #if dm.hasNameItems():
#   #  for name in dm.iterNameItems():
#   #    text = text.replace(name.text, " %s " % name.text)
#   #return text.strip()
#
#   Temporarily disabled for being slow
#  def queryLatinWordTerms(self, text):
#    """
#    @param  text  unicode
#    @return  unicode or None
#    """
#    d = self.__d
#    if not d.enabled or d.locked:
#      return text
#    dm = dataman.manager()
#    for term in dm.iterLatinSourceTerms():
#      td = term.d
#      if (not td.hentai or d.hentai) and td.pattern and not td.regex and td.language == 'en':
#        if text == td.pattern and td.text:
#          return td.text.capitalize() if td.type == 'name' else td.text
#
#   Temporarily disabled for being slow
#  def queryFuriTerms(self, text):
#    """
#    @param  text  unicode
#    @return  unicode or None
#    """
#    d = self.__d
#    if not d.enabled or d.locked:
#      return text
#    dm = dataman.manager()
#    for term in dm.iterFuriTerms():
#      td = term.d
#      if (not td.hentai or d.hentai) and td.pattern and not td.regex and td.language == 'ja': # skip using regular expressions
#        if text == td.pattern and td.text:
#          return td.text
#
#   #if dm.hasNameItems():
#    #  for name in dm.iterNameItems():
#    #    if text == name.text:
#    #      return name.yomi or text
#
#  def applyTerms(self, terms, text, language, convertsChinese=False, marksChanges=False):
#    """
#    @param  terms  iterable dataman.Term
#    @param  text  unicode
#    @param  language  unicode
#    @param* convertsChinese  bool
#    @param*  marksChanges  bool  mark the replacement text
#    @return  unicode
#    """
#    if not self.enabled:
#      return text
#    dm = dataman.manager()
#    hasTitles = dm.hasTermTitles() # cached
#    zht = language == 'zht' # cached
#    for term in terms:
#      td = term.d # To improve performance
#      if (not td.hentai or self.hentai) and td.pattern and i18n.language_compatible_to(td.language, language):
#        if hasTitles and term.needsReplace():
#          try: text = term.replace(text)
#          except Exception, e: dwarn(td.pattern, td.text, e)
#        else:
#          z = convertsChinese and zht and td.language == 'zhs'
#          #repl = term.bbcodeText if term.bbcode else term.text
#          repl = td.text
#          if repl:
#            if z: # and self.convertsChinese:
#              repl = zhs2zht(repl)
#            #elif config.is_latin_language(td.language):
#            #  repl += " "
#            if marksChanges:
#              repl = _mark_text(repl)
#          if not term.patternNeedsRe():
#            pattern = zhs2zht(td.pattern) if z else td.pattern
#            text = text.replace(pattern, repl)
#          else:
#            try: text = term.patternRe.sub(repl, text)
#            except Exception, e: dwarn(td.pattern, td.text, e)
#        if not text: # well, the text is deleted by terms
#          break
#    return text
#
#  def iterTerms(self, terms, language):
#    """
#    @param  terms  iterable dataman.Term
#    @param  language  unicode
#    @yield  Term
#    """
#    if not self.enabled:
#      return
#    for term in terms:
#      td = term.d # To improve performance
#      if (not td.hentai or self.hentai) and td.pattern and i18n.language_compatible_to(td.language, language):
#        yield term
#
#  def warmup(self, terms, language='', hasTitles=False, hentai=False): # [dataman.Term], str, bool ->
#    dprint("enter")
#    needsEscape = config.is_asian_language(language)
#    for term in terms:
#      td = term.d # To improve performance
#      if not td.disabled and not td.special and (not td.hentai or hentai) and td.pattern and i18n.language_compatible_to(td.language, language):
#        if hasTitles and term.needsReplace():
#          try:
#            if needsEscape:
#              term.prepareReplace
#              term.applyReplace
#            else:
#              term.replace
#          except Exception, e: dwarn(td.pattern, td.text, e)
#        elif td.pattern and term.patternNeedsRe():
#          try: term.patternRe
#          except Exception, e: dwarn(td.pattern, td.text, e)
#
#    import trman
#    trman.manager().clearCacheRequested.emit()
#
#    self.locked = False
#    dprint("leave")
#
