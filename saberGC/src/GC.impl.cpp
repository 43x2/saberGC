// GC.impl.cpp

#include "saber/GC.h"
#include <cstdint>
#include <cstdlib>
#include <map>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include <cassert>


namespace saber {

namespace {

using pointer_value_type = std::uintptr_t;

template <class T>
class ContainerAllocator
{
	template <class U> friend class ContainerAllocator;

public:
	using value_type = T;

public:
	ContainerAllocator(GC::Impl* impl);
	template <class U> ContainerAllocator(const ContainerAllocator<U>& other);

	T* allocate(const std::size_t count);
	void deallocate(T* pointer, const std::size_t count);

private:
	GC::Impl* impl_;
};

class AllocatedStorage
{
public:
	AllocatedStorage(void* allocated, const std::size_t size, void(*destructor)(void*), GC::Impl* impl);
	AllocatedStorage(const AllocatedStorage&) = delete;
	AllocatedStorage(AllocatedStorage&& other);
	~AllocatedStorage();
	AllocatedStorage& operator=(const AllocatedStorage&) = delete;
	AllocatedStorage& operator=(AllocatedStorage&&) = delete;

	std::size_t get_size() const;

	void add_child(void* object);

	bool is_marked() const;
	void mark();
	void unmark();

private:
	void* allocated_;
	std::size_t size_;
	void (*destructor_)(void*);
	GC::Impl* impl_;

	std::unordered_set<pointer_value_type, std::hash<pointer_value_type>, std::equal_to<>, ContainerAllocator<pointer_value_type>> child_objects_;
	bool is_marked_;
};

} // namespace


class GC::Impl
{
public:
	Impl(allocator_type allocator, deallocator_type deallocator);
	Impl(const Impl&) = delete;
	Impl(Impl&&) = delete;
	~Impl();
	Impl& operator=(const Impl&) = delete;
	Impl& operator=(Impl&&) = delete;

	//	from GC
	void collect();
	void* allocate(const std::size_t size, const std::size_t alignment);

	//	Impl only
	void add_new_object(void* object, void* allocated, const std::size_t size, void(*destructor)(void*));
	void copy_object(void* to, const void* from);
	void copy_and_assign_object(void* to, const void* from);
	void remove_object(void* object);
	void deallocate(void* pointer);
	void mark_child_object(const pointer_value_type child_object);

private:
	allocator_type allocator_;
	deallocator_type deallocator_;

	std::map<pointer_value_type, AllocatedStorage, std::greater<>, ContainerAllocator<std::pair<const pointer_value_type, AllocatedStorage>>> storages_;

	using storages_iterator_type = decltype(storages_)::iterator;
	std::unordered_map<pointer_value_type, storages_iterator_type, std::hash<pointer_value_type>, std::equal_to<>, ContainerAllocator<std::pair<const pointer_value_type, storages_iterator_type>>> root_objects_;
	decltype(root_objects_) child_objects_;

	std::mutex mutex_;
};


GC::GC()
	: GC(
		/*allocator*/ [](const std::size_t size, const std::size_t alignment) {
			auto pointer = std::malloc(size);
			assert((reinterpret_cast<std::uintptr_t>(pointer) & (alignment - 1)) == 0); // alignment assertion
			return pointer;
		},
		/*deallocator*/ [](void* pointer) {
			std::free(pointer);
		})
{
}

GC::GC(allocator_type allocator, deallocator_type deallocator)
{
	auto pointer = allocator(sizeof(Impl), alignof(Impl));
	auto impl = new (pointer) Impl(std::move(allocator), deallocator);
	decltype(impl_)(impl, [deallocator = std::move(deallocator)](Impl* pointer) {
		destruct<Impl>(pointer);
		deallocator(pointer);
	}).swap(impl_);
}

GC::GC(GC&&) = default;

GC::~GC() = default;

GC& GC::operator=(GC&&) = default;

void GC::collect()
{
	impl_->collect();
}

void* GC::allocate(const std::size_t size, const std::size_t alignment)
{
	return impl_->allocate(size, alignment);
}


GC::BaseObject::BaseObject()
	: impl_{ nullptr }
{
}

GC::BaseObject::BaseObject(GC::Impl* impl, void* allocated, const std::size_t size, void(*destructor)(void*))
	: impl_{ impl }
{
	assert(impl_);
	impl_->add_new_object(this, allocated, size, destructor);
}

GC::BaseObject::BaseObject(const BaseObject& other)
	: impl_{ other.impl_ }
{
	if (impl_) {
		impl_->copy_object(this, &other);
	}
}

GC::BaseObject::~BaseObject()
{
	if (impl_) {
		impl_->remove_object(this);
	}
}

GC::BaseObject& GC::BaseObject::operator=(const BaseObject& rhs)
{
	if (this != &rhs) {
		if (impl_) {
			if (impl_ == rhs.impl_) {
				impl_->copy_and_assign_object(this, &rhs);
			} else {
				impl_->remove_object(this);
				impl_ = rhs.impl_;
				if (impl_) {
					impl_->copy_object(this, &rhs);
				}
			}
		} else {
			if (rhs.impl_) {
				// Similar to copy constructor.
				impl_ = rhs.impl_;
				impl_->copy_object(this, &rhs);
			}
		}
	}
	return *this;
}


template <class T>
ContainerAllocator<T>::ContainerAllocator(GC::Impl* impl)
	: impl_{ impl }
{
}

template <class T>
template <class U>
ContainerAllocator<T>::ContainerAllocator(const ContainerAllocator<U>& other)
	: impl_{ other.impl_ }
{
}

template <class T>
T* ContainerAllocator<T>::allocate(const std::size_t count)
{
	return static_cast<T*>(impl_->allocate(sizeof(T) * count, alignof(T)));
}

template <class T>
void ContainerAllocator<T>::deallocate(T* pointer, const std::size_t /*count*/)
{
	impl_->deallocate(pointer);
}

AllocatedStorage::AllocatedStorage(void* allocated, const std::size_t size, void(*destructor)(void*), GC::Impl* impl)
	: allocated_{ allocated }
	, size_{ size }
	, destructor_{ destructor }
	, impl_{ impl }
	, child_objects_{ ContainerAllocator<decltype(child_objects_)::value_type>(impl_) }
	, is_marked_{ true }
{
}

AllocatedStorage::AllocatedStorage(AllocatedStorage&& other)
	: allocated_{ other.allocated_ }
	, size_{ other.size_ }
	, destructor_{ other.destructor_ }
	, impl_{ other.impl_ }
	, child_objects_{ std::move(other.child_objects_) }
	, is_marked_{ other.is_marked_ }
{
	other.allocated_ = nullptr; // to prevent double-freeing.
}

AllocatedStorage::~AllocatedStorage()
{
	if (allocated_) {
		destructor_(allocated_);
		impl_->deallocate(allocated_);
	}
}

std::size_t AllocatedStorage::get_size() const
{
	return size_;
}

void AllocatedStorage::add_child(void* object)
{
	assert(object);
	auto object_uint = reinterpret_cast<decltype(child_objects_)::key_type>(object);
	auto emplaced = child_objects_.emplace(object_uint);
	assert(emplaced.second);
}

bool AllocatedStorage::is_marked() const
{
	return is_marked_;
}

void AllocatedStorage::mark()
{
	if (!is_marked_) {
		is_marked_ = true;
		for (auto&& child_object : child_objects_) {
			impl_->mark_child_object(child_object);
		}
	}
}

void AllocatedStorage::unmark()
{
	is_marked_ = false;
}


GC::Impl::Impl(allocator_type allocator, deallocator_type deallocator)
	: allocator_{ std::move(allocator) }
	, deallocator_{ std::move(deallocator) }
	, storages_{ ContainerAllocator<decltype(storages_)::value_type>(this) }
	, root_objects_{ ContainerAllocator<decltype(root_objects_)::value_type>(this) }
	, child_objects_{ ContainerAllocator<decltype(child_objects_)::value_type>(this) }
{
}

GC::Impl::~Impl()
{
	collect();

	assert(storages_.size() == 0);
	assert(root_objects_.size() == 0);
	assert(child_objects_.size() == 0);
}

void GC::Impl::collect()
{
	// Preparing.
	for (auto&& storage : storages_) {
		storage.second.unmark();
	}
	// Mark phase.
	for (auto&& object : root_objects_) {
		object.second->second.mark();
	}
	// Sweep phase.
	for (auto it = storages_.begin(); it != storages_.end();) {
		if (it->second.is_marked()) {
			++it;
		} else {
			it = storages_.erase(it);
		}
	}
}

void* GC::Impl::allocate(const std::size_t size, const std::size_t alignment)
{
	auto pointer = allocator_(size, alignment);
	if (!pointer) {
		collect();
		pointer = allocator_(size, alignment);
	}
	return pointer;
}

void GC::Impl::add_new_object(void* object, void* allocated, const std::size_t size, void(*destructor)(void*))
{
	assert(object && allocated && size > 0 && destructor);

	std::lock_guard<std::mutex> locker(mutex_);

	auto emplaced0 = storages_.emplace(reinterpret_cast<pointer_value_type>(allocated), AllocatedStorage(allocated, size, destructor, this));
	assert(emplaced0.second);

	auto object_uint = reinterpret_cast<pointer_value_type>(object);
	auto lb = storages_.lower_bound(object_uint);
	if (lb != storages_.end()) {
		if (object_uint >= lb->first && object_uint < lb->first + lb->second.get_size()) {
			assert(lb != emplaced0.first);
			lb->second.add_child(object);
			auto emplaced1 = child_objects_.emplace(object_uint, emplaced0.first);
			assert(emplaced1.second);
			return;
		}
	}
	auto emplaced1 = root_objects_.emplace(object_uint, emplaced0.first);
	assert(emplaced1.second);
}

void GC::Impl::copy_object(void* to, const void* from)
{
	assert(to && from && to != from);

	std::lock_guard<std::mutex> locker(mutex_);

	auto from_uint = reinterpret_cast<pointer_value_type>(from);
	auto iterator = storages_.end();
	{
		auto found = root_objects_.find(from_uint);
		if (found != root_objects_.end()) {
			iterator = found->second;
		} else {
			found = child_objects_.find(from_uint);
			if (found != child_objects_.end()) {
				iterator = found->second;
			}
		}
	}
	assert(iterator != storages_.end());

	auto to_uint = reinterpret_cast<pointer_value_type>(to);
	auto lb = storages_.lower_bound(to_uint);
	if (lb != storages_.end()) {
		if (to_uint >= lb->first && to_uint < lb->first + lb->second.get_size()) {
			lb->second.add_child(to);
			auto emplaced = child_objects_.emplace(to_uint, iterator);
			assert(emplaced.second);
			return;
		}
	}
	auto emplaced = root_objects_.emplace(to_uint, iterator);
	assert(emplaced.second);
}

void GC::Impl::copy_and_assign_object(void* to, const void* from)
{
	assert(to && from && to != from);

	std::lock_guard<std::mutex> locker(mutex_);

	auto from_uint = reinterpret_cast<pointer_value_type>(from);
	auto iterator = storages_.end();
	{
		auto found = root_objects_.find(from_uint);
		if (found != root_objects_.end()) {
			iterator = found->second;
		} else {
			found = child_objects_.find(from_uint);
			if (found != child_objects_.end()) {
				iterator = found->second;
			}
		}
	}
	assert(iterator != storages_.end());

	auto to_uint = reinterpret_cast<pointer_value_type>(to);
	{
		auto found = root_objects_.find(to_uint);
		if (found != root_objects_.end()) {
			found->second = iterator;
		} else {
			found = child_objects_.find(to_uint);
			assert(found != child_objects_.end());
			found->second = iterator;
		}
	}
}

void GC::Impl::remove_object(void* object)
{
	assert(object);

	std::lock_guard<std::mutex> locker(mutex_);

	auto object_uint = reinterpret_cast<pointer_value_type>(object);
	auto erased = root_objects_.erase(object_uint);
	if (erased == 0) {
		erased = child_objects_.erase(object_uint);
	}
	assert(erased == 1);
}

void GC::Impl::deallocate(void* pointer)
{
	if (pointer) {
		deallocator_(pointer);
	}
}

void GC::Impl::mark_child_object(const pointer_value_type child_object)
{
	auto found = child_objects_.find(child_object);
	assert(found != child_objects_.end());
	found->second->second.mark();
}

} // namespace saber
