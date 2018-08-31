// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file has been auto-generated by code_generator_v8.py. DO NOT MODIFY!

// clang-format off
#ifndef TestInterfaceOrTestInterfaceEmpty_h
#define TestInterfaceOrTestInterfaceEmpty_h

#include "bindings/core/v8/Dictionary.h"
#include "bindings/core/v8/ExceptionState.h"
#include "bindings/core/v8/V8Binding.h"
#include "core/CoreExport.h"
#include "platform/heap/Handle.h"

namespace blink {

class TestInterfaceEmpty;
class TestInterfaceImplementation;

class CORE_EXPORT TestInterfaceOrTestInterfaceEmpty final {
    DISALLOW_NEW_EXCEPT_PLACEMENT_NEW();
public:
    TestInterfaceOrTestInterfaceEmpty();
    bool isNull() const { return m_type == SpecificTypeNone; }

    bool isTestInterface() const { return m_type == SpecificTypeTestInterface; }
    TestInterfaceImplementation* getAsTestInterface() const;
    void setTestInterface(TestInterfaceImplementation*);
    static TestInterfaceOrTestInterfaceEmpty fromTestInterface(TestInterfaceImplementation*);

    bool isTestInterfaceEmpty() const { return m_type == SpecificTypeTestInterfaceEmpty; }
    TestInterfaceEmpty* getAsTestInterfaceEmpty() const;
    void setTestInterfaceEmpty(TestInterfaceEmpty*);
    static TestInterfaceOrTestInterfaceEmpty fromTestInterfaceEmpty(TestInterfaceEmpty*);

    TestInterfaceOrTestInterfaceEmpty(const TestInterfaceOrTestInterfaceEmpty&);
    ~TestInterfaceOrTestInterfaceEmpty();
    TestInterfaceOrTestInterfaceEmpty& operator=(const TestInterfaceOrTestInterfaceEmpty&);
    DECLARE_TRACE();

private:
    enum SpecificTypes {
        SpecificTypeNone,
        SpecificTypeTestInterface,
        SpecificTypeTestInterfaceEmpty,
    };
    SpecificTypes m_type;

    Member<TestInterfaceImplementation> m_testInterface;
    Member<TestInterfaceEmpty> m_testInterfaceEmpty;

    friend CORE_EXPORT v8::Local<v8::Value> toV8(const TestInterfaceOrTestInterfaceEmpty&, v8::Local<v8::Object>, v8::Isolate*);
};

class V8TestInterfaceOrTestInterfaceEmpty final {
public:
    CORE_EXPORT static void toImpl(v8::Isolate*, v8::Local<v8::Value>, TestInterfaceOrTestInterfaceEmpty&, UnionTypeConversionMode, ExceptionState&);
};

CORE_EXPORT v8::Local<v8::Value> toV8(const TestInterfaceOrTestInterfaceEmpty&, v8::Local<v8::Object>, v8::Isolate*);

template <class CallbackInfo>
inline void v8SetReturnValue(const CallbackInfo& callbackInfo, TestInterfaceOrTestInterfaceEmpty& impl)
{
    v8SetReturnValue(callbackInfo, toV8(impl, callbackInfo.Holder(), callbackInfo.GetIsolate()));
}

template <>
struct NativeValueTraits<TestInterfaceOrTestInterfaceEmpty> {
    CORE_EXPORT static TestInterfaceOrTestInterfaceEmpty nativeValue(v8::Isolate*, v8::Local<v8::Value>, ExceptionState&);
};

} // namespace blink

// We need to set canInitializeWithMemset=true because HeapVector supports
// items that can initialize with memset or have a vtable. It is safe to
// set canInitializeWithMemset=true for a union type object in practice.
// See https://codereview.chromium.org/1118993002/#msg5 for more details.
WTF_ALLOW_MOVE_AND_INIT_WITH_MEM_FUNCTIONS(blink::TestInterfaceOrTestInterfaceEmpty);

#endif // TestInterfaceOrTestInterfaceEmpty_h
