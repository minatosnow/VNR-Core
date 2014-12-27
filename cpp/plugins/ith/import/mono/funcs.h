#pragma once

// mono/funcs.h
// 12/26/2014
// https://github.com/mono/mono/blob/master/mono/metadata/object.h
// http://api.xamarin.com/index.aspx?link=xhtml%3Adeploy%2Fmono-api-string.html

#include "ith/import/mono/types.h"


// MonoString*    mono_string_new            (MonoDomain *domain,
//                                            const char *text);
// MonoString*    mono_string_new_len        (MonoDomain *domain,
//                                            const char *text,
//                                            guint length);
// MonoString*    mono_string_new_size       (MonoDomain *domain,
//                                            gint32 len);
// MonoString*    mono_string_new_utf16      (MonoDomain *domain,
//                                            const guint16 *text,
//                                            gint32 len);
// MonoString*    mono_string_from_utf16     (gunichar2 *data);
// mono_unichar2* mono_string_to_utf16       (MonoString *s);
// char*          mono_string_to_utf8        (MonoString *s);
// gboolean       mono_string_equal          (MonoString *s1,
//                                            MonoString *s2);
// guint          mono_string_hash           (MonoString *s);
// MonoString*    mono_string_intern         (MonoString *str);
// MonoString*    mono_string_is_interned    (MonoString *o);
// MonoString*    mono_string_new_wrapper    (const char *text);
// gunichar2*     mono_string_chars          (MonoString *s);
// int            mono_string_length         (MonoString *s);
// gunichar2*     mono_unicode_from_external (const gchar *in,
//                                            gsize *bytes);

typedef mono_unichar2*  (* mono_string_to_utf16_fun_t)  (MonoString *s);
typedef char*           (* mono_string_to_utf8_fun_t)   (MonoString *s);

// EOF
