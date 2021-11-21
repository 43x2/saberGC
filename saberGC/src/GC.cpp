// GC.cpp

#include "saber/GC.h"
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <unordered_map>

#if defined(__cpp_exceptions)
#define SABER_GC_TRY        try
#define SABER_GC_CATCH_ALL  catch (...)
#else // defined(__cpp_exceptions)
#define SABER_GC_TRY        if constexpr (true)
#define SABER_GC_CATCH_ALL  if constexpr (false)
#endif // defined(__cpp_exceptions)

#if !defined(SABER_GC_ASSERT)
#include <cassert>
#define SABER_GC_ASSERT(condition)  assert(condition)
#endif // !defined(SABER_GC_ASSERT)


namespace saber {

class GC::Impl
{
public:
	Impl(std::pmr::memory_resource* resource);
	Impl(const Impl&) = delete;
	~Impl();
	Impl& operator=(const Impl&) = delete;

	//	from GC
	void collect();

	//	functions without lock
	std::pair<void*, bool> new_object(const BaseObject* object, const std::size_t bytes, const std::size_t alignment);
	void set_destructor(const void* storage, void(*destructor)(void*));

	//	functions with lock
	std::unique_lock<std::mutex> lock();
	bool copy_object(const BaseObject* to, const BaseObject* from, const bool overwrite, const std::unique_lock<std::mutex>& locker);
	void remove_object(const BaseObject* object, const std::unique_lock<std::mutex>& locker);
	void mark_child_object(const BaseObject* object, const std::unique_lock<std::mutex>& locker);

private:
	class Storage;
	using storage_container_type = std::pmr::map<const void*, Storage, std::greater<>>;
	using storage_iterator_type = typename storage_container_type::iterator;

	using object_container_type = std::pmr::unordered_map<const BaseObject*, storage_iterator_type>;

private:
	bool add_object(const BaseObject* object, storage_iterator_type iterator, const bool overwrite, const std::unique_lock<std::mutex>& locker);

private:
	std::pmr::memory_resource* resource_;

	storage_container_type storages_;
	object_container_type root_objects_;
	object_container_type child_objects_;

	std::mutex mutex_;
};

class GC::Impl::Storage
{
public:
	Storage(const std::size_t bytes, const std::size_t alignment, Impl* impl);
	Storage(Storage&& other) noexcept;
	~Storage();
	Storage& operator=(const Storage&) = delete;

	//	functions without lock of Impl
	void* get_pointer() const noexcept;
	std::size_t get_bytes() const noexcept;

	//	functions with lock of Impl
	void set_destructor(void(*destructor)(void*), const std::unique_lock<std::mutex>& locker) noexcept;
	void add_child(const BaseObject* object, const std::unique_lock<std::mutex>& locker);
	bool is_marked(const std::unique_lock<std::mutex>& locker) const noexcept;
	void mark(const std::unique_lock<std::mutex>& locker);
	void unmark(const std::unique_lock<std::mutex>& locker) noexcept;

private:
	void* pointer_;
	std::size_t bytes_;
	std::size_t alignment_;
	void (*destructor_)(void*){ nullptr };
	Impl* impl_;

	std::pmr::deque<const BaseObject*> child_objects_;
	bool is_marked_{ true };
};


GC::GC(std::pmr::memory_resource* resource)
{
	if (!resource) {
		resource = std::pmr::get_default_resource();
	}
	SABER_GC_ASSERT(resource);
	impl_ = std::allocate_shared<Impl>(std::pmr::polymorphic_allocator<Impl>{ resource }, resource);
}

GC::GC(GC&&) noexcept = default;

GC::~GC() = default;

GC& GC::operator=(GC&&) noexcept = default;

void GC::collect()
{
	impl_->collect();
}


GC::Impl::Impl(std::pmr::memory_resource* resource)
	: resource_{ resource }
	, storages_{ resource }
	, root_objects_{ resource }
	, child_objects_{ resource }
{
}

GC::Impl::~Impl()
{
	// There must be no root objects because a root object has a shared_ptr<Impl>.
	// On the other hand, there can be child objects which will be destroyed at following GC.
	SABER_GC_ASSERT(root_objects_.size() == 0);

	collect();

	SABER_GC_ASSERT(storages_.size() == 0);
}

void GC::Impl::collect()
{
	// Moving erasing containers to this container prevents the mutex from being double-locked.
	std::pmr::deque<Storage> erased_storages{ resource_ };

	auto locker = lock();

	// Preparing.
	for (auto&& storage : storages_) {
		storage.second.unmark(locker);
	}
	// Mark phase.
	for (auto&& object : root_objects_) {
		object.second->second.mark(locker);
	}
	// Sweep phase.
	for (auto it = storages_.begin(); it != storages_.end();) {
		if (it->second.is_marked(locker)) {
			++it;
		} else {
			erased_storages.push_back(std::move(it->second));
			it = storages_.erase(it);
		}
	}
}

std::pair<void*, bool> GC::Impl::new_object(const BaseObject* object, const std::size_t bytes, const std::size_t alignment)
{
	Storage storage{ bytes, alignment, this };
	auto pointer = storage.get_pointer();

	auto locker = lock();

	auto emplaced = storages_.emplace(pointer, std::move(storage));
	SABER_GC_ASSERT(emplaced.second);

	return { pointer, add_object(object, emplaced.first, false, locker) };
}

void GC::Impl::set_destructor(const void* storage, void(*destructor)(void*))
{
	SABER_GC_ASSERT(storage);

	auto locker = lock();

	auto iterator = storages_.find(storage);
	SABER_GC_ASSERT(iterator != storages_.end());

	iterator->second.set_destructor(destructor, locker);
}

std::unique_lock<std::mutex> GC::Impl::lock()
{
	return std::unique_lock<std::mutex>{ mutex_ };
}

bool GC::Impl::copy_object(const BaseObject* to, const BaseObject* from, const bool overwrite, const std::unique_lock<std::mutex>& locker)
{
	SABER_GC_ASSERT(from && locker && locker.mutex() == &mutex_);

	auto iterator = ([](auto object, auto impl) {
		auto found = impl->root_objects_.find(object);
		if (found != impl->root_objects_.end()) {
			return found->second;
		}

		found = impl->child_objects_.find(object);
		if (found != impl->child_objects_.end()) {
			return found->second;
		}

		return impl->storages_.end();
	})(from, this);

	SABER_GC_ASSERT(iterator != storages_.end());

	return add_object(to, iterator, overwrite, locker);
}

void GC::Impl::remove_object(const BaseObject* object, [[maybe_unused]] const std::unique_lock<std::mutex>& locker)
{
	SABER_GC_ASSERT(object && locker && locker.mutex() == &mutex_);

	auto erased = root_objects_.erase(object);
	if (erased == 0) {
		erased = child_objects_.erase(object);
	}
	SABER_GC_ASSERT(erased == 1);
}

void GC::Impl::mark_child_object(const BaseObject* object, const std::unique_lock<std::mutex>& locker)
{
	SABER_GC_ASSERT(object && locker && locker.mutex() == &mutex_);

	auto found = child_objects_.find(object);
	if (found != child_objects_.end()) {
		found->second->second.mark(locker);
	}
}

bool GC::Impl::add_object(const BaseObject* object, storage_iterator_type iterator, const bool overwrite, const std::unique_lock<std::mutex>& locker)
{
	SABER_GC_ASSERT(object && locker && locker.mutex() == &mutex_);

	auto is_root_object = true;

	// Object is a child if it is inside of existing storage.
	auto lb = storages_.lower_bound(object);
	if (lb != storages_.end()) {
		auto end_of_storage = static_cast<const void*>(static_cast<const std::byte*>(lb->first) + lb->second.get_bytes());
		if (object >= lb->first && object < end_of_storage) {
			lb->second.add_child(object, locker);
			is_root_object = false;
		}
	}

	if (overwrite) {
		(is_root_object ? root_objects_ : child_objects_).insert_or_assign(object, iterator);
	}
	else {
		auto emplaced = (is_root_object ? root_objects_ : child_objects_).emplace(object, iterator);
		SABER_GC_ASSERT(emplaced.second);
	}

	return is_root_object;
}


GC::Impl::Storage::Storage(const std::size_t bytes, const std::size_t alignment, Impl* impl)
	: pointer_{ nullptr }
	, bytes_{ bytes }
	, alignment_{ alignment }
	, impl_{ impl }
	, child_objects_{ impl->resource_ }
{
	SABER_GC_ASSERT(impl);

	SABER_GC_TRY {
		pointer_ = impl->resource_->allocate(bytes, alignment);
	}
	SABER_GC_CATCH_ALL {
		impl->collect();
		pointer_ = impl->resource_->allocate(bytes, alignment); // There is no way to handle...
	}
}

GC::Impl::Storage::Storage(Storage&& other) noexcept
	: pointer_{ other.pointer_ }
	, bytes_{ other.bytes_ }
	, alignment_{ other.alignment_ }
	, destructor_{ other.destructor_ }
	, impl_{ other.impl_ }
	, child_objects_{ std::move(other.child_objects_) }
	, is_marked_{ other.is_marked_ }
{
	other.pointer_ = nullptr; // Preventing from double-freeing.
}

GC::Impl::Storage::~Storage()
{
	if (pointer_) {
		if (destructor_) {
			destructor_(pointer_);
		}
		impl_->resource_->deallocate(pointer_, bytes_, alignment_);
	}
}

void* GC::Impl::Storage::get_pointer() const noexcept
{
	return pointer_;
}

std::size_t GC::Impl::Storage::get_bytes() const noexcept
{
	return bytes_;
}

void GC::Impl::Storage::set_destructor(void(*destructor)(void*), [[maybe_unused]] const std::unique_lock<std::mutex>& locker) noexcept
{
	SABER_GC_ASSERT(destructor && locker && locker.mutex() == &impl_->mutex_);
	SABER_GC_ASSERT(!destructor_);

	destructor_ = destructor;
}

void GC::Impl::Storage::add_child(const BaseObject* object, [[maybe_unused]] const std::unique_lock<std::mutex>& locker)
{
	SABER_GC_ASSERT(object && locker && locker.mutex() == &impl_->mutex_);

	child_objects_.push_back(object);
}

bool GC::Impl::Storage::is_marked([[maybe_unused]] const std::unique_lock<std::mutex>& locker) const noexcept
{
	SABER_GC_ASSERT(locker && locker.mutex() == &impl_->mutex_);

	return is_marked_;
}

void GC::Impl::Storage::mark(const std::unique_lock<std::mutex>& locker)
{
	SABER_GC_ASSERT(locker && locker.mutex() == &impl_->mutex_);

	if (!is_marked_) {
		is_marked_ = true;
		for (auto&& child_object : child_objects_) {
			impl_->mark_child_object(child_object, locker);
		}
	}
}

void GC::Impl::Storage::unmark([[maybe_unused]] const std::unique_lock<std::mutex>& locker) noexcept
{
	SABER_GC_ASSERT(locker && locker.mutex() == &impl_->mutex_);

	is_marked_ = false;
}


GC::BaseObject::BaseObject() noexcept
	: storage_{ nullptr }
{
}

GC::BaseObject::BaseObject(const std::shared_ptr<Impl>& impl, const std::size_t bytes, const std::size_t alignment)
{
	SABER_GC_ASSERT(impl);

	auto result = impl->new_object(this, bytes, alignment);

	storage_ = result.first;

	if (result.second) {
		impl_ = std::shared_ptr<Impl>{ impl };
	}
	else {
		impl_ = std::weak_ptr<Impl>{ impl };
	}
}

GC::BaseObject::BaseObject(const BaseObject& other)
	: storage_{ other.storage_ }
	, impl_{ other.impl_ }
{
	if (storage_) {
		std::visit([this, &other](auto&& impl) {
			using T = std::decay_t<decltype(impl)>;

			if constexpr (std::is_same_v<T, std::shared_ptr<Impl>>) {
				auto locker = impl->lock();
				if (!impl->copy_object(this, &other, false, locker)) {
					// Switch to weak_ptr<Impl> since this is a child object.
					impl_ = std::weak_ptr<Impl>{ impl };
				}
			}
			else if constexpr (std::is_same_v<T, std::weak_ptr<Impl>>) {
				auto shared = std::shared_ptr<Impl>{ impl };
				auto locker = shared->lock();
				if (shared->copy_object(this, &other, false, locker)) {
					// Switch to shared_ptr<Impl> since this is a root object.
					impl_ = std::move(shared);
				}
			}
		}, impl_);
	}
}

GC::BaseObject::~BaseObject()
{
	if (storage_) {
		std::visit([this](auto&& impl) {
			using T = std::decay_t<decltype(impl)>;

			if constexpr (std::is_same_v<T, std::shared_ptr<Impl>>) {
				auto locker = impl->lock();
				impl->remove_object(this, locker);
			}
			else if constexpr (std::is_same_v<T, std::weak_ptr<Impl>>) {
				// Note that shared_ptr<Impl> can not be obtained
				// if this function is called from the destructor of Impl.
				if (auto shared = impl.lock()) {
					auto locker = shared->lock();
					shared->remove_object(this, locker);
				}
			}
		}, impl_);
	}
}

GC::BaseObject& GC::BaseObject::operator=(const BaseObject& rhs)
{
	if (this != &rhs) {
		auto old_storage = storage_;
		auto old_impl = std::move(impl_);

		storage_ = rhs.storage_;
		impl_ = rhs.impl_;
		Impl* pImpl = nullptr;

		if (storage_) {
			std::visit([this, &rhs, &pImpl](auto&& impl) {
				using T = std::decay_t<decltype(impl)>;

				if constexpr (std::is_same_v<T, std::shared_ptr<Impl>>) {
					pImpl = impl.get();
					auto locker = impl->lock();
					if (!impl->copy_object(this, &rhs, true, locker)) {
						// Switch to weak_ptr<Impl> since this is a child object.
						impl_ = std::weak_ptr<Impl>{ impl };
					}
				}
				else if constexpr (std::is_same_v<T, std::weak_ptr<Impl>>) {
					auto shared = std::shared_ptr<Impl>{ impl };
					pImpl = shared.get();
					auto locker = shared->lock();
					if (shared->copy_object(this, &rhs, true, locker)) {
						// Switch to shared_ptr<Impl> since this is a root object.
						impl_ = std::move(shared);
					}
				}
			}, impl_);
		}

		if (old_storage) {
			std::visit([this, pImpl](auto&& impl) {
				using T = std::decay_t<decltype(impl)>;

				if constexpr (std::is_same_v<T, std::shared_ptr<Impl>>) {
					if (impl.get() != pImpl) {
						auto locker = impl->lock();
						impl->remove_object(this, locker);
					}
				}
				else if constexpr (std::is_same_v<T, std::weak_ptr<Impl>>) {
					auto shared = std::shared_ptr<Impl>{ impl };
					if (shared.get() != pImpl) {
						auto locker = shared->lock();
						shared->remove_object(this, locker);
					}
				}
			}, old_impl);
		}
	}
	return *this;
}

void GC::BaseObject::set_destructor(void(*destructor)(void*))
{
	SABER_GC_ASSERT(destructor);

	SABER_GC_TRY {
		std::visit([this, destructor](auto&& impl) {
			using T = std::decay_t<decltype(impl)>;

			if constexpr (std::is_same_v<T, std::shared_ptr<Impl>>) {
				impl->set_destructor(storage_, destructor);
			}
			else if constexpr (std::is_same_v<T, std::weak_ptr<Impl>>) {
				std::shared_ptr<Impl>{ impl }->set_destructor(storage_, destructor);
			}
		}, impl_);
	}
	SABER_GC_CATCH_ALL {
		destructor(storage_);
		throw;
	}
}

void GC::BaseObject::reset()
{
	if (storage_) {
		std::visit([this](auto&& impl) {
			using T = std::decay_t<decltype(impl)>;

			if constexpr (std::is_same_v<T, std::shared_ptr<Impl>>) {
				auto locker = impl->lock();
				impl->remove_object(this, locker);
			}
			else if constexpr (std::is_same_v<T, std::weak_ptr<Impl>>) {
				auto shared = std::shared_ptr<Impl>{ impl };
				auto locker = shared->lock();
				shared->remove_object(this, locker);
			}
		}, impl_);

		storage_ = nullptr;
		impl_ = nullptr;
	}
}

} // namespace saber
