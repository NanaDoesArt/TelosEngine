#pragma once
#include <EASTL/shared_ptr.h>
#include <EASTL/weak_ptr.h>
#include <EASTL/unique_ptr.h>
#include <cstdint>
#include <type_traits>

class Object;

template <typename T>
class TWeakPtr;

template <typename T>
class TSharedPtr
{
public:
    TSharedPtr() = default;
    TSharedPtr(std::nullptr_t) : Ptr(nullptr) {}
    explicit TSharedPtr(T* RawPtr) : Ptr(RawPtr)
    {
        static_assert(!std::is_base_of_v<Object, T>, "TSharedPtr cannot manage RObject subclasses! Use NewObject<T>(Outer) and TWeakObjectPtr<T> instead.");
    }

    ~TSharedPtr()
    {
        static_assert(!std::is_base_of_v<Object, T>, "TSharedPtr cannot manage RObject subclasses! Use NewObject<T>(Outer) and TWeakObjectPtr<T> instead.");
    }

    TSharedPtr(const TSharedPtr&) = default;
    TSharedPtr& operator=(const TSharedPtr&) = default;
    TSharedPtr(TSharedPtr&&) noexcept = default;
    TSharedPtr& operator=(TSharedPtr&&) noexcept = default;

    template <typename Derived, typename = std::enable_if_t<std::is_convertible_v<Derived*, T*>>>
    TSharedPtr(const TSharedPtr<Derived>& Other) : Ptr(Other.Ptr) {}

    bool IsValid() const { return Ptr != nullptr; }
    explicit operator bool() const { return IsValid(); }

    T* Get() const { return Ptr.get(); }
    T* operator->() const { return Ptr.get(); }
    T& operator*() const { return *Ptr; }

    void Reset() { Ptr.reset(); }
    int32_t GetSharedReferenceCount() const { return static_cast<int32_t>(Ptr.use_count()); }

private:
    explicit TSharedPtr(const eastl::shared_ptr<T>& InPtr) : Ptr(InPtr) {}

    template <typename U>
    friend class TSharedPtr;

    template <typename U>
    friend class TWeakPtr;

    template <typename U, typename... Args>
    friend TSharedPtr<U> MakeShared(Args&&... args);

    eastl::shared_ptr<T> Ptr;
};

template <typename T>
class TWeakPtr
{
public:
    TWeakPtr() = default;
    TWeakPtr(const TSharedPtr<T>& Shared) : Ptr(Shared.Ptr)
    {
        static_assert(!std::is_base_of_v<Object, T>, "TWeakPtr cannot reference RObject subclasses! Use TWeakObjectPtr<T> instead.");
    }

    ~TWeakPtr()
    {
        static_assert(!std::is_base_of_v<Object, T>, "TWeakPtr cannot reference RObject subclasses! Use TWeakObjectPtr<T> instead.");
    }

    TWeakPtr(const TWeakPtr&) = default;
    TWeakPtr& operator=(const TWeakPtr&) = default;
    TWeakPtr(TWeakPtr&&) noexcept = default;
    TWeakPtr& operator=(TWeakPtr&&) noexcept = default;

    TSharedPtr<T> Pin() const { return TSharedPtr<T>(Ptr.lock()); }
    bool IsValid() const { return !Ptr.expired(); }
    void Reset() { Ptr.reset(); }

private:
    eastl::weak_ptr<T> Ptr;
};

template <typename T>
class TUniquePtr
{
public:
    TUniquePtr() = default;
    TUniquePtr(std::nullptr_t) : Ptr(nullptr) {}
    explicit TUniquePtr(T* RawPtr) : Ptr(RawPtr)
    {
        static_assert(!std::is_base_of_v<Object, T>, "TUniquePtr cannot manage RObject subclasses! Use NewObject<T>(Outer) instead.");
    }

    ~TUniquePtr()
    {
        static_assert(!std::is_base_of_v<Object, T>, "TUniquePtr cannot manage RObject subclasses! Use NewObject<T>(Outer) instead.");
    }

    TUniquePtr(const TUniquePtr&) = delete;
    TUniquePtr& operator=(const TUniquePtr&) = delete;

    TUniquePtr(TUniquePtr&&) noexcept = default;
    TUniquePtr& operator=(TUniquePtr&&) noexcept = default;

    bool IsValid() const { return Ptr != nullptr; }
    explicit operator bool() const { return IsValid(); }

    T* Get() const { return Ptr.get(); }
    T* operator->() const { return Ptr.get(); }
    T& operator*() const { return *Ptr; }

    T* Release() { return Ptr.release(); }
    void Reset(T* NewPtr = nullptr) { Ptr.reset(NewPtr); }

private:
    eastl::unique_ptr<T> Ptr;
};

template <typename T, typename... Args>
TSharedPtr<T> MakeShared(Args&&... args)
{
    static_assert(!std::is_base_of_v<Object, T>, "RObject subclasses cannot use MakeShared! Use NewObject<T>(Outer) instead.");
    return TSharedPtr<T>(eastl::make_shared<T>(eastl::forward<Args>(args)...));
}

template <typename T, typename... Args>
TUniquePtr<T> MakeUnique(Args&&... args)
{
    static_assert(!std::is_base_of_v<Object, T>, "RObject subclasses cannot use MakeUnique! Use NewObject<T>(Outer) instead.");
    return TUniquePtr<T>(new T(eastl::forward<Args>(args)...));
}