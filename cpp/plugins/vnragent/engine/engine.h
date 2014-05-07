#pragma once

// engine.h
// 4/20/2014 jichi

#include "sakurakit/skglobal.h"
#include "engine/enginedef.h"
#include <QtCore/QObject>

class EngineSettings
{
public:
  bool enabled;
  bool textVisible[Engine::RoleCount],
       transcodingEnabled[Engine::RoleCount],
       translationEnabled[Engine::RoleCount];

  EngineSettings()
    : enabled(false)
  {
    qMemSet(textVisible, 0, sizeof(textVisible));
    qMemSet(transcodingEnabled, 0, sizeof(textVisible));
    qMemSet(translationEnabled, 0, sizeof(textVisible));
  }
};

class AbstractEnginePrivate;
class AbstractEngine
{
  //Q_OBJECT
  SK_CLASS(AbstractEngine)
  SK_DECLARE_PRIVATE(AbstractEnginePrivate)
  SK_DISABLE_COPY(AbstractEngine)

public:
  static Self *instance(); // Needed to be explicitly deleted on exit

  AbstractEngine(const char *name, const char *encoding);
  virtual ~AbstractEngine();

  EngineSettings *settings();

  const char *name() const;
  const char *encoding() const;

  virtual bool inject() = 0;
  virtual bool unload() = 0;

  //static bool isEnabled();
  //static void setEnabled(bool t);

public: // only needed by descendants
  QByteArray dispatchTextA(const QByteArray &data, int role, bool blocking = true) const;
  //QString dispatchTextW(const QQString &text, int role, bool blocking = true) const;
};

// EOF

//signals:
//  // context is opaque, and the receiver of this signal is responsible to release the context
//  void textReceived(const QString &text, qint64 hash, int role, void *context);
//public:
//  virtual void drawText(const QString &text, const void *context) = 0;
//  virtual void releaseContext(void *context) = 0;
