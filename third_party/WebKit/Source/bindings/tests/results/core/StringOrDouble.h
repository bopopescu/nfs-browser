// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file has been auto-generated by code_generator_v8.py. DO NOT MODIFY!

// clang-format off
#ifndef StringOrDouble_h
#define StringOrDouble_h

#include "bindings/core/v8/Dictionary.h"
#include "bindings/core/v8/ExceptionState.h"
#include "bindings/core/v8/V8Binding.h"
#include "core/CoreExport.h"
#include "platform/heap/Handle.h"

namespace blink {

class CORE_EXPORT StringOrDouble final {
    DISALLOW_NEW_EXCEPT_PLACEMENT_NEW();
public:
    StringOrDouble();
    bool isNull() const { return m_type == SpecificTypeNone; }

    bool isString() const { return m_type == SpecificTypeString; }
    String getAsString() const;
    void setString(String);
    static StringOrDouble fromString(String);

    bool isDouble() const { return m_type == SpecificTypeDouble; }
    double getAsDouble() const;
    void setDouble(double);
    static StringOrDouble fromDouble(double);

    StringOrDouble(const StringOrDouble&);
    ~StringOrDouble();
    StringOrDouble& operator=(const StringOrDouble&);
    DECLARE_TRACE();

private:
    enum SpecificTypes {
        SpecificTypeNone,
        SpecificTypeString,
        SpecificTypeDouble,
    };
    SpecificTypes m_type;

    String m_string;
    double m_double;

    friend CORE_EXPORT v8::Local<v8::Value> toV8(const StringOrDouble&, v8::Local<v8::Object>, v8::Isolate*);
};

class V8StringOrDouble final {
public:
    CORE_EXPORT static void toImpl(v8::Isolate*, v8::Local<v8::Value>, StringOrDouble&, UnionTypeConversionMode, ExceptionState&);
};

CORE_EXPORT v8::Local<v8::Value> toV8(const StringOrDouble&, v8::Local<v8::Object>, v8::Isolate*);

template <class CallbackInfo>
inline void v8SetReturnValue(const CallbackInfo& callbackInfo, StringOrDouble& impl)
{
    v8SetReturnValue(callbackInfo, toV8(impl, callbackInfo.Holder(), callbackInfo.GetIsolate()));
}

template <>
struct NativeValueTraits<StringOrDouble> {
    CORE_EXPORT static StringOrDouble nativeValue(v8::Isolate*, v8::Local<v8::Value>, ExceptionState&);
};

} // namespace blink

// We need to set canInitializeWithMemset=true because HeapVector supports
// items that can initialize with memset or have a vtable. It is safe to
// set canInitializeWithMemset=true for a union type object in practice.
// See https://codereview.chromium.org/1118993002/#msg5 for more details.
WTF_ALLOW_MOVE_AND_INIT_WITH_MEM_FUNCTIONS(blink::StringOrDouble);

#endif // StringOrDouble_h
