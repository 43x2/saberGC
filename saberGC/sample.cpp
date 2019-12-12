
#include <memory>
#include <utility>
#include "saber/GC.h"


struct B;

struct A
{
	saber::GC::Object<B> a0;

	A(std::unique_ptr<saber::GC>& gc)
		: a0{ gc->new_object<B>(gc) }
	{
	}
};

struct B
{
	saber::GC::Object<int> b0;
	saber::GC::Object<A> b1;

	B(std::unique_ptr<saber::GC>& gc)
		: b0{ gc->new_object<int>(42) }
	{
	}
};


int main()
{
	auto gc = std::make_unique<saber::GC>();
	{
		auto obj0 = gc->new_object<A>(gc);
		auto obj1 = obj0;
		auto obj2 = std::move(obj1);

		obj0->a0->b1 = obj0;

		saber::GC::Object<A> obj3, obj4;
		obj3 = obj0;
		obj4 = std::move(obj2);
		obj3 = obj4;

		auto obj5 = gc->new_object<int>(334);
		obj0->a0->b0 = std::move(obj5);
	}
	gc->collect();
	return 0;
}
