// saber/GC.h
#pragma once

#include <cstddef>
#include <memory>
#include <memory_resource>
#include <type_traits>
#include <utility>
#include <variant>


namespace saber {

namespace emulated {

// Emulation of C++20 std::is_unbounded_array
template <class>
struct is_unbounded_array : std::false_type {};
template <class T>
struct is_unbounded_array<T[]> : std::true_type {};
template <class T>
constexpr bool is_unbounded_array_v = is_unbounded_array<T>::value;

// Emulation of C++20 std::destroy_at
template <class T>
constexpr void destroy_at(T* p)
{
	if constexpr (std::is_array_v<T>) {
		for (auto&& e : *p) {
			destroy_at(std::addressof(e));
		}
	}
	else {
		p->~T();
	}
}

} // namespace emulated


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
	template <class T, class... Args>
	std::enable_if_t<!std::is_array_v<T> && !std::is_void_v<T>, Object<T>> new_object(Args&&... args);

	// Allocates and constructs a new array.
	template <class T>
	std::enable_if_t<emulated::is_unbounded_array_v<T>, Object<T>> new_array(const std::size_t count);

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
	BaseObject(const std::shared_ptr<Impl>& impl, const std::size_t size, const std::size_t alignment, const std::size_t count);
	BaseObject(const BaseObject& other);
	~BaseObject();
	BaseObject& operator=(const BaseObject& rhs);

	void set_destructor(void(*destructor)(void*, const std::size_t), const std::size_t count);
	void reset();

protected:
	void* storage_;

private:
	std::variant<std::shared_ptr<Impl>, std::weak_ptr<Impl>> impl_;
	std::size_t count_;
};

template <class T>
class GC::Object : protected GC::BaseObject
{
	static_assert(!std::is_array_v<T> && !std::is_void_v<T> || emulated::is_unbounded_array_v<T>);

	friend GC;

public:
	using element_type = std::remove_extent_t<T>;

public:
	Object() noexcept = default;
	Object(const Object&) = default;
	~Object() = default;
	Object& operator=(const Object&) = default;

	// Constructs from an other type object.
	template <class U, class = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	Object(const Object<U>& other)
		: BaseObject{ other }
	{
		storage_ = static_cast<element_type*>(other.get());
	}

	// Assigns from an other type object.
	template <class U, class = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	Object& operator=(const Object<U>& rhs)
	{
		BaseObject::operator=(rhs);
		storage_ = static_cast<element_type*>(rhs.get());
		return *this;
	}

	// Returns the pointer of storage.
	element_type* get() const noexcept
	{
		return static_cast<element_type*>(storage_);
	}

	// Returns the pointer of storage for accessing members.
	template <class U = T, class = std::enable_if_t<!std::is_array_v<U> && !std::is_void_v<U>>>
	U* operator->() const noexcept
	{
		return get();
	}

	// Dereferences the pointer of storage.
	template <class U = T, class = std::enable_if_t<!std::is_array_v<U> && !std::is_void_v<U>>>
	U& operator*() const noexcept
	{
		return *get();
	}

	// Returns the reference of nth element of the array in storage.
	template <class U = T, class = std::enable_if_t<emulated::is_unbounded_array_v<U>>>
	element_type& operator[](const std::ptrdiff_t index) const noexcept
	{
		return get()[index];
	}

	// Checks if the pointer is not null.
	explicit operator bool() const noexcept
	{
		return get() != nullptr;
	}

	// Releases the ownership.
	void reset()
	{
		BaseObject::reset();
	}

private:
	template <class U = T, std::enable_if_t<!std::is_array_v<U> && !std::is_void_v<U>, int> = 0, class... Args>
	Object(const std::shared_ptr<Impl>& impl, Args&&... args);
	template <class U = T, std::enable_if_t<emulated::is_unbounded_array_v<U>, int> = 0>
	Object(const std::shared_ptr<Impl>& impl, const std::size_t count);

	static void destruct(void* p, const std::size_t count)
	{
		if constexpr (!std::is_trivially_destructible_v<element_type>) {
			if (p) {
				for (auto i = decltype(count){ 0 }; i < count; ++i) {
					emulated::destroy_at(static_cast<element_type*>(p) + i);
				}
			}
		}
	}
};


template <class T, class... Args>
std::enable_if_t<!std::is_array_v<T> && !std::is_void_v<T>, GC::Object<T>> GC::new_object(Args&& ...args)
{
	return { impl_, std::forward<Args>(args)... };
}

template <class T>
std::enable_if_t<emulated::is_unbounded_array_v<T>, GC::Object<T>> GC::new_array(const std::size_t count)
{
	return { impl_, count };
}


template <class T>
template <class U, std::enable_if_t<!std::is_array_v<U> && !std::is_void_v<U>, int>, class... Args>
GC::Object<T>::Object(const std::shared_ptr<Impl>& impl, Args&&... args)
	: BaseObject{ impl, sizeof(element_type), alignof(element_type), 0 }
{
	storage_ = new (storage_) element_type{ std::forward<Args>(args)... };
	set_destructor(&destruct, 0);
}

template <class T>
template <class U, std::enable_if_t<emulated::is_unbounded_array_v<U>, int>>
GC::Object<T>::Object(const std::shared_ptr<Impl>& impl, const std::size_t count)
	: BaseObject{ impl, sizeof(element_type), alignof(element_type), count }
{
	storage_ = new (storage_) element_type[count] {};
	set_destructor(&destruct, count);
}

} // namespace saber
