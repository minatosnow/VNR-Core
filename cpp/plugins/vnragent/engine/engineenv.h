#pragma once

// engineenv.h
// 4/20/2014 jichi

#include <QtCore/QString>

namespace Engine {

// File system
bool glob(const QString &nameFilter);
bool glob(const QStringList &nameFilters);

bool glob(const QString &relPath, const QString &nameFilter);
bool glob(const QString &relPath, const QStringList &nameFilters);

// Thread and process

QString getNormalizedProcessName();

bool getMemoryRange(const wchar_t *moduleName, unsigned long *startAddress, unsigned long *stopAddress);

// This function might be cached and hence not thread-safe
unsigned long getModuleFunction(const char *moduleName, const char *funcName);

} // namespace Engine

// EOF
