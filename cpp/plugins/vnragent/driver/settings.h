#pragma once

// settings.h
// 4/1/2014 jichi

#include "sakurakit/skglobal.h"
#include <QtCore/QObject>

class SettingsPrivate;
// Root object for all qobject
class Settings : public QObject
{
  Q_OBJECT
  Q_DISABLE_COPY(Settings)
  SK_EXTEND_CLASS(Settings, QObject)
  SK_DECLARE_PRIVATE(SettingsPrivate)
public:
  static Self *instance();

  explicit Settings(QObject *parent = nullptr);
  ~Settings();

  QString language() const;
  void setLanguage(const QString &v);
};

// EOF
