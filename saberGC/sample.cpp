
#include <iostream>
#include <new>
#include "saber/GC.h"

#define USE_MEMORY_RESOURCE


#if defined(USE_MEMORY_RESOURCE)
class TestMemoryResource : public std::pmr::memory_resource
{
private:
	void* do_allocate(const std::size_t bytes, const std::size_t alignment) override
	{
		auto p = operator new(bytes, std::align_val_t{ alignment });
		std::clog << "do_allocate(bytes: " << bytes << ", alignment: " << alignment << ") returns " << p << "\n";
		return p;
	}

	void do_deallocate(void* p, const std::size_t bytes, const std::size_t alignment) override
	{
		std::clog << "do_deallocate(p: " << p << ", bytes: " << bytes << ", alignment: " << alignment << ")\n";
		operator delete(p, bytes, std::align_val_t{ alignment });
	}

	bool do_is_equal(const std::pmr::memory_resource&) const noexcept override
	{
		std::clog << "do_is_equal(...)\n";
		return true;
	}
};
#endif // defined(USE_MEMORY_RESOURCE)

struct Base
{
	saber::GC::Object<Base> base_;

	Base()
	{
		std::cerr << "Base\n";
	}

	~Base()
	{
		std::cerr << "~Base\n";
	}
};

struct Derived : Base
{
	Derived()
	{
		std::cerr << "Derived\n";
	}

	~Derived()
	{
		std::cerr << "~Derived\n";
	}
};


int main()
{
#if defined(USE_MEMORY_RESOURCE)
	TestMemoryResource tmr;
	saber::GC gc{ &tmr };
#else // defined(USE_MEMORY_RESOURCE)
	saber::GC gc;
#endif // defined(USE_MEMORY_RESOURCE)

	auto derived = gc.new_object<Derived>();
	saber::GC::Object<Base> base = derived;
	base->base_ = derived; // It's a cyclic reference, but no problem.

	return 0;
} // saber::GC collects garbages implicitly when destroyed.
