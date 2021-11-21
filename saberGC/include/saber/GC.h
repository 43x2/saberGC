// saber/GC.h
#pragma once

#include <cstddef>
#include <memory>
#include <memory_resource>
#include <type_traits>
#include <utility>
#include <variant>


namespace saber {

class GC
{
public:
	template <class T> class Object;

public:
	explicit GC(std::pmr::memory_resource* resource = nullptr);
	GC(GC&&) noexcept;
	~GC();
	GC& operator=(GC&&) noexcept;

	// Allocates and constructs a new object.
	template <class T, class... Args> Object<T> new_object(Args&&... args);

	// Destructs and deallocates unreferenced objects explicitly.
	void collect();

private:
	class BaseObject;
	class Impl;

private:
	std::shared_ptr<Impl> impl_;
};

class GC::BaseObject
{
public:
	BaseObject() noexcept;
	BaseObject(const std::shared_ptr<Impl>& impl, const std::size_t bytes, const std::size_t alignment);
	BaseObject(const BaseObject& other);
	~BaseObject();
	BaseObject& operator=(const BaseObject& rhs);

	void set_destructor(void(*destructor)(void*));
	void reset();

protected:
	void* storage_;

private:
	std::variant<std::shared_ptr<Impl>, std::weak_ptr<Impl>> impl_;
};

template <class T>
class GC::Object : protected GC::BaseObject
{
	friend GC;

public:
	Object() noexcept = default;
	Object(const Object&) = default;
	~Object() = default;
	Object& operator=(const Object&) = default;

	// Returns the pointer of storage.
	T* get() const noexcept;

	// Returns the pointer of storage for accessing members.
	T* operator->() const noexcept;

	// Dereferences the pointer of storage.
	T& operator*() const noexcept;

	// Checks if the pointer is not null.
	explicit operator bool() const noexcept;

	// Releases the ownership.
	void reset();

private:
	template <class... Args> Object(const std::shared_ptr<Impl>& impl, Args&&... args);
	static void destruct(void* p);
};


template <class T, class... Args>
GC::Object<T> GC::new_object(Args&&... args)
{
	return { impl_, std::forward<Args>(args)... };
}


template <class T>
T* GC::Object<T>::get() const noexcept
{
	return static_cast<T*>(storage_);
}

template <class T>
T* GC::Object<T>::operator->() const noexcept
{
	return get();
}

template <class T>
T& GC::Object<T>::operator*() const noexcept
{
	return *get();
}

template <class T>
GC::Object<T>::operator bool() const noexcept
{
	return get() != nullptr;
}

template <class T>
void GC::Object<T>::reset()
{
	BaseObject::reset();
}

template <class T>
template <class... Args>
GC::Object<T>::Object(const std::shared_ptr<Impl>& impl, Args&&... args)
	: BaseObject{ impl, sizeof(T), alignof(T) }
{
	new (storage_) T{ std::forward<Args>(args)... };
	set_destructor(&destruct);
}

template <class T>
void GC::Object<T>::destruct(void* p)
{
	if constexpr (!std::is_trivially_destructible_v<T>) {
		if (p) {
			static_cast<T*>(p)->~T();
		}
	}
}

} // namespace saber
