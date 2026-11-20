#pragma once

#include <cstddef>
#include <utility>

namespace geekdesk::platform {

/// Lightweight COM smart pointer compatible with both MinGW-w64 and MSVC without WRL.
template <typename T>
class ComPtr {
public:
    ComPtr() noexcept = default;
    ComPtr(std::nullptr_t) noexcept {}

    explicit ComPtr(T* ptr) noexcept : ptr_(ptr) {
        InternalAddRef();
    }

    ComPtr(const ComPtr& other) noexcept : ptr_(other.ptr_) {
        InternalAddRef();
    }

    ComPtr(ComPtr&& other) noexcept : ptr_(other.ptr_) {
        other.ptr_ = nullptr;
    }

    ~ComPtr() {
        InternalRelease();
    }

    ComPtr& operator=(std::nullptr_t) noexcept {
        InternalRelease();
        return *this;
    }

    ComPtr& operator=(const ComPtr& other) noexcept {
        if (this != &other) {
            ComPtr(other).Swap(*this);
        }
        return *this;
    }

    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            ComPtr(std::move(other)).Swap(*this);
        }
        return *this;
    }

    void Swap(ComPtr& other) noexcept {
        std::swap(ptr_, other.ptr_);
    }

    void Reset() noexcept {
        InternalRelease();
    }

    void Attach(T* ptr) noexcept {
        if (ptr_ != ptr) {
            InternalRelease();
            ptr_ = ptr;
        }
    }

    [[nodiscard]] T* Detach() noexcept {
        T* tmp = ptr_;
        ptr_ = nullptr;
        return tmp;
    }

    [[nodiscard]] T* Get() const noexcept {
        return ptr_;
    }

    [[nodiscard]] T* const* GetAddressOf() const noexcept {
        return &ptr_;
    }

    [[nodiscard]] T** GetAddressOf() noexcept {
        return &ptr_;
    }

    [[nodiscard]] T** ReleaseAndGetAddressOf() noexcept {
        InternalRelease();
        return &ptr_;
    }

    [[nodiscard]] T* operator->() const noexcept {
        return ptr_;
    }

    explicit operator bool() const noexcept {
        return ptr_ != nullptr;
    }

private:
    void InternalAddRef() noexcept {
        if (ptr_ != nullptr) {
            ptr_->AddRef();
        }
    }

    void InternalRelease() noexcept {
        if (ptr_ != nullptr) {
            T* tmp = ptr_;
            ptr_ = nullptr;
            tmp->Release();
        }
    }

    T* ptr_ = nullptr;
};

} // namespace geekdesk::platform
