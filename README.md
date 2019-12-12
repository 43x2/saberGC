# saberGC

**saberGC** is a prototype of garbage collection system in C++.

## Features
- Naïve mark-and-sweep garbage collection.
- Custom allocator/deallocator with alignment support.
- `shared_ptr`/`unique_ptr`-like interface.

## Requirements
- C++14 (MSVC, clang, gcc, etc.)

## Usage
```cpp
struct Foo
{
    saber::GC::Object<int> int_;
    saber::GC::Object<Foo> foo_;

    Foo(std::unique_ptr<saber::GC>& gc)
        : int_{ gc->new_object<int>(42) }
    {
    }
};

int main()
{
    auto gc = std::make_unique<saber::GC>();
    auto foo = gc->new_object<Foo>(gc);
    foo->foo_ = foo; // cyclic reference!
    return 0;
} // collects garbages implicitly when destruction.
```

## ToDos
- Array type construction support.
- Exception safety support.
