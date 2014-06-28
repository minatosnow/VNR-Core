#pragma once

// engineutil.h
// 4/20/2014 jichi

#include <QtCore/QString>

namespace Engine {

// Detours
typedef void *address_type;
typedef const void *const_address_type;

///  Replace the instruction at the old_addr with jmp to new_addr. Return the address to the replaced code.
address_type replaceFunction(address_type old_addr, const_address_type new_addr);
//address_type restoreFunction(address_type restore_addr, const_address_type old_addr);

// Ignore type checking
template<typename Ret, typename Arg1, typename Arg2>
inline Ret replaceFunction(Arg1 arg1, Arg2 arg2)
{ return (Ret)replaceFunction((address_type)arg1, (const_address_type)arg2); }

// Not used
//template<typename Ret, typename Arg1, typename Arg2>
//inline Ret restoreFunction(Arg1 arg1, Arg2 arg2)
//{ return (Ret)restoreFunction((address_type)arg1, (const_address_type)arg2); }

// File system
bool globs(const QString &nameFilter);
bool globs(const QStringList &nameFilters);

bool globs(const QString &relPath, const QString &nameFilter);
bool globs(const QString &relPath, const QStringList &nameFilters);

bool exists(const QString &relPath);
bool exists(const QStringList &relPaths);

// Thread and process

QString getNormalizedProcessName();

bool getMemoryRange(const wchar_t *moduleName, unsigned long *startAddress, unsigned long *stopAddress);

inline bool getCurrentMemoryRange(unsigned long *startAddress, unsigned long *stopAddress)
{ return getMemoryRange(nullptr, startAddress, stopAddress); }

// This function might be cached and hence not thread-safe
unsigned long getModuleFunction(const char *moduleName, const char *funcName);

} // namespace Engine

// EOF