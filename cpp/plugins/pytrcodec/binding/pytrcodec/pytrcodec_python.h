

#ifndef SBK_PYTRCODEC_PYTHON_H
#define SBK_PYTRCODEC_PYTHON_H

#include <sbkpython.h>
#include <conversions.h>
#include <sbkenum.h>
#include <basewrapper.h>
#include <bindingmanager.h>
#include <memory>

// Binded library includes
#include <trcodec.h>
// Conversion Includes - Primitive Types
#include <string>

// Type indices
#define SBK_TRANSLATIONCODEC_IDX                                     0
#define SBK_pytrcodec_IDX_COUNT                                      1

// This variable stores all Python types exported by this module.
extern PyTypeObject** SbkpytrcodecTypes;

// This variable stores all type converters exported by this module.
extern SbkConverter** SbkpytrcodecTypeConverters;

// Converter indices
#define SBK_STD_WSTRING_IDX                                          0
#define SBK_pytrcodec_CONVERTERS_IDX_COUNT                           1

// Macros for type check

namespace Shiboken
{

// PyType functions, to get the PyObjectType for a type T
template<> inline PyTypeObject* SbkType< ::TranslationCodec >() { return reinterpret_cast<PyTypeObject*>(SbkpytrcodecTypes[SBK_TRANSLATIONCODEC_IDX]); }

} // namespace Shiboken

#endif // SBK_PYTRCODEC_PYTHON_H

