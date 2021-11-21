# saberGC

**saberGC** is a prototype of garbage collection system in C++.

## Features
- Naïve mark-and-sweep and *exact* garbage collection.
- `shared_ptr`/`unique_ptr`-like interface.
- Custom `memory_resource` support.
- Not a singleton and no global variables.
	- There can be multiple GC instances if necessary.

## Requirements
- C++17 (clang, MSVC, gcc, etc.)
	- e.g. `memory_resource`, `polymorphic_allocator`, `variant`, `[[maybe_unused]]`

## Usage
```cpp
struct Foo
{
	saber::GC::Object<Foo> foo_;
};

int main()
{
	saber::GC gc;

	auto foo = gc.new_object<Foo>();
	foo->foo_ = foo; // It's a cyclic reference, but no problem.

	return 0;
} // saber::GC collects garbages implicitly when destroyed.
```

## ToDos
- Array type construction support.
