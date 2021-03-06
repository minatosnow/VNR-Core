

#ifndef SBK_PYTEXTHOOK_PYTHON_H
#define SBK_PYTEXTHOOK_PYTHON_H

#include <sbkpython.h>
#include <conversions.h>
#include <sbkenum.h>
#include <basewrapper.h>
#include <bindingmanager.h>
#include <memory>

#include <pysidesignal.h>
// Module Includes
#include <pyside_qtcore_python.h>
#include <pyside_qtgui_python.h>

// Binded library includes
#include <texthook.h>
// Conversion Includes - Primitive Types
#include <QString>
#include <signalmanager.h>
#include <typeresolver.h>
#include <QtConcurrentFilter>
#include <QStringList>
#include <qabstractitemmodel.h>

// Conversion Includes - Container Types
#include <QList>
#include <QMap>
#include <QStack>
#include <QMultiMap>
#include <QVector>
#include <QPair>
#include <pysideconversions.h>
#include <QSet>
#include <QQueue>
#include <QLinkedList>

// Type indices
#define SBK_TEXTHOOK_IDX                                             0
#define SBK_pytexthook_IDX_COUNT                                     1

// This variable stores all Python types exported by this module.
extern PyTypeObject** SbkpytexthookTypes;

// This variable stores all type converters exported by this module.
extern SbkConverter** SbkpytexthookTypeConverters;

// Converter indices
#define SBK_PYTEXTHOOK_QLIST_QOBJECTPTR_IDX                          0 // const QList<QObject * > &
#define SBK_PYTEXTHOOK_QLIST_QBYTEARRAY_IDX                          1 // QList<QByteArray >
#define SBK_PYTEXTHOOK_QLIST_QINT32_IDX                              2 // const QList<qint32 > &
#define SBK_PYTEXTHOOK_QLIST_QVARIANT_IDX                            3 // QList<QVariant >
#define SBK_PYTEXTHOOK_QLIST_QSTRING_IDX                             4 // QList<QString >
#define SBK_PYTEXTHOOK_QMAP_QSTRING_QVARIANT_IDX                     5 // QMap<QString, QVariant >
#define SBK_pytexthook_CONVERTERS_IDX_COUNT                          6

// Macros for type check

namespace Shiboken
{

// PyType functions, to get the PyObjectType for a type T
template<> inline PyTypeObject* SbkType< ::TextHook >() { return reinterpret_cast<PyTypeObject*>(SbkpytexthookTypes[SBK_TEXTHOOK_IDX]); }

} // namespace Shiboken

#endif // SBK_PYTEXTHOOK_PYTHON_H

