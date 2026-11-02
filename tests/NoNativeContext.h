#pragma once
#include <type_traits>
class CKContext;
class CKObject;
class CKBehavior;
class CKParameter;
class CKRenderContext;
template <typename T, typename = void> struct CompleteNativeType : std::false_type {};
template <typename T> struct CompleteNativeType<T, std::void_t<decltype(sizeof(T))>> : std::true_type {};
static_assert(!CompleteNativeType<CKContext>::value, "Presentation cannot access CKContext");
static_assert(!CompleteNativeType<CKObject>::value, "Presentation cannot access CKObject");
static_assert(!CompleteNativeType<CKBehavior>::value, "Presentation cannot access CKBehavior");
static_assert(!CompleteNativeType<CKParameter>::value, "Presentation cannot access CKParameter");
static_assert(!CompleteNativeType<CKRenderContext>::value, "Presentation cannot access CKRenderContext");
