// saber/GC.h
#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <utility>


namespace saber {

class GC
{
public:
	template <class T> class Object;
	class Impl;
private:
	class BaseObject;

public:
	using allocator_type = std::function<void*(const std::size_t size, const std::size_t alignment)>;
	using deallocator_type = std::function<void(void* pointer)>;

public:
	GC();
	GC(allocator_type allocator, deallocator_type deallocator);
	GC(const GC&) = delete;
	GC(GC&&);
	~GC();
	GC& operator=(const GC&) = delete;
	GC& operator=(GC&&);

	// Allocates and constructs a new object.
	template <class T, class... Args> Object<T> new_object(Args&&... args);

	// Destructs and deallocates no longer used objects explicitly.
	void collect();

private:
	void* allocate(const std::size_t size, const std::size_t alignment);
	template <class T> static void destruct(void* pointer);

private:
	std::unique_ptr<Impl, std::function<void(Impl*)>> impl_;
};

class GC::BaseObject
{
	template <class T> friend class Object;

private:
	BaseObject();
	BaseObject(GC::Impl* impl, void* allocated, const std::size_t size, void(*destructor)(void*));
	BaseObject(const BaseObject& other);
	~BaseObject();
	BaseObject& operator=(const BaseObject& rhs);

private:
	GC::Impl* impl_;
};

template <class T>
class GC::Object : private GC::BaseObject
{
	friend GC;

public:
	Object() = default;
	Object(const Object&) = default;
	~Object() = default;
	Object& operator=(const Object& rhs);

	T* get() const;
	T& operator*() const;
	T* operator->() const;

private:
	template <class... Args> Object(void* allocated, GC::Impl* impl, Args&&... args);

private:
	T* instance_{ nullptr };
};


template <class T, class... Args>
GC::Object<T> GC::new_object(Args&&... args)
{
	return Object<T>(allocate(sizeof(T), alignof(T)), impl_.get(), std::forward<Args>(args)...);
}

template <class T>
void GC::destruct(void* pointer)
{
	if (pointer) {
		static_cast<T*>(pointer)->~T();
	}
}


template <class T>
GC::Object<T>& GC::Object<T>::operator=(const Object& rhs)
{
	BaseObject::operator=(rhs);
	instance_ = rhs.instance_;
	return *this;
}

template <class T>
T* GC::Object<T>::get() const
{
	return instance_;
}

template <class T>
T& GC::Object<T>::operator*() const
{
	return *get();
}

template <class T>
T* GC::Object<T>::operator->() const
{
	return get();
}

template <class T>
template <class... Args>
GC::Object<T>::Object(void* allocated, GC::Impl* impl, Args&&... args)
	: BaseObject(impl, allocated, sizeof(T), &destruct<T>)
	, instance_{ new (allocated) T(std::forward<Args>(args)...) }
{
}

} // namespace saber
