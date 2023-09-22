#include <iostream>
#include <atomic>
#include <assert.h>
#include <memory>
#include <thread>

class Entity;

template<typename T> class SharedPointer;


class A {
public:
    std::shared_ptr<Entity> m_shared;
    A() {
        std::cout << "Entity created" << std::endl;
    }

    ~A() {
        std::cout << "Entity destroyed" << std::endl;
    }
};

class Entity {
public:
    std::shared_ptr<A> m_shared;
    std::weak_ptr<A> m_weak;
    Entity() {
        std::cout << "Entity created" << std::endl;
    }

    ~Entity() {
        std::cout << "Entity destroyed" << std::endl;
    }
};

template <typename T>
class SharedPointerControlBlock {
public:
    T* ptr = nullptr;
    std::atomic<int> refCount = 0;
    std::atomic<int> weakRefCount = 0;

    SharedPointerControlBlock(T* pointer) : ptr(pointer), refCount(1) {}

    bool incrementCounter() {
        int refCountValue = refCount.load();
        if (refCount == 0) {
            return false;
        }
        int conflictCounter = 0;
        while (!std::atomic_compare_exchange_weak(refCount, refCountValue, refCountValue + 1)) {
            if (conflictCounter++ > 5) {
                std::this_thread::yield();
                conflictCounter = 0;
            }
            refCountValue = refCount.load();
            if (refCount == 0) {
                return false;
            }
        }
        return true;
    }

    bool decrementCounter() {
        int refCountValue = refCount.load();
        if (refCount <= 0) {
            throw std::runtime_error("Decrement counter error");
        }
        int conflictCounter = 0;
        while (!std::atomic_compare_exchange_weak(refCount, refCountValue, refCountValue - 1)) {
            if (conflictCounter++ > 5) {
                std::this_thread::yield();
                conflictCounter = 0;
            }
            refCountValue = refCount.load();
            if (refCount <= 0) {
                throw std::runtime_error("Decrement counter error");
            }
        }
        if (refCount == 0) {
            delete ptr;
        }
        return true;
    }

    bool incrementWeakCounter() {
        int weakCountValue = weakRefCount.load();
        if (weakRefCount == 0) {
            return false;
        }
        int conflictCounter = 0;
        while (!std::atomic_compare_exchange_weak(weakRefCount, weakCountValue, weakCountValue + 1)) {
            if (conflictCounter++ > 5) {
                std::this_thread::yield();
                conflictCounter = 0;
            }
            weakCountValue = weakRefCount.load();
            if (weakRefCount == 0) {
                return false;
            }
        }
        return true;
    }

    bool decrementWeakCounter() {
        int weakCountValue = weakRefCount.load();
        int conflictCounter = 0;
        while (!std::atomic_compare_exchange_weak(weakRefCount, weakCountValue, weakCountValue - 1)) {
            if (conflictCounter++ > 5) {
                std::this_thread::yield();
                conflictCounter = 0;
            }
            weakCountValue = weakRefCount.load();
        }
        if (weakRefCount == 0 && refCount == 0) {
            delete this;
        }
    }

    SharedPointer<T> make_shared_from_weak();

    void free() {
        assert(refCount > 0);
        refCount--;
        if (refCount == 0) {
            delete ptr;
            if (weakRefCount == 0) {
                delete this;
            }
        }
    }
};

template <typename T>
class SharedPointer {

public:
    SharedPointerControlBlock<T>* m_controlBlock = nullptr;

    SharedPointer(T* pointer) {
        m_controlBlock = new SharedPointerControlBlock(pointer);
    }

    SharedPointer() {}

    operator bool() const { return m_controlBlock != nullptr && m_controlBlock->refCount > 0; }

    template <class... _Args>
    static SharedPointer makeShared(_Args&&... __args) {
        return SharedPointer(new T(std::forward<_Args>(__args)...));
    }


    SharedPointer(const SharedPointer<T>& other) {
        if (other.m_controlBlock != nullptr) {
            other.m_controlBlock->incrementCounter();
            m_controlBlock = other.m_controlBlock;
        }
    }

    SharedPointer<T>& operator=(const SharedPointer<T>& other) {
        if (this != &other) {
            if (m_controlBlock != nullptr) {
                m_controlBlock->free();
            }
            if (other.m_controlBlock != nullptr) {
                other.m_controlBlock->incrementCounter();
                m_controlBlock = other.m_controlBlock;
            }
        }
        return *this;
    }

    int use_count() {
        return m_controlBlock->refCount;
    }

    void reset() {
        if (m_controlBlock != nullptr) {
            m_controlBlock->decrementCounter();
            m_controlBlock = nullptr;
        }
    }

    ~SharedPointer() {
        if (m_controlBlock != nullptr) {
            m_controlBlock->decrementCounter();
        }
    }
};

template <typename T>
SharedPointer<T> SharedPointerControlBlock<T>::make_shared_from_weak() {
    return SharedPointer<T>();
}

template <typename T>
class WeakPointer {
    SharedPointerControlBlock<T>* m_controlBlock = nullptr;

public:
    WeakPointer() {}

    WeakPointer(const SharedPointer<T>& shared) : m_controlBlock(shared.m_controlBlock) {
        if (m_controlBlock != nullptr) {
            m_controlBlock->incrementWeakCounter();
        }
    }

    WeakPointer<T>& operator=(const SharedPointer<T>& shared) {
        if (this != &shared) {
            m_controlBlock = shared.m_controlBlock;
            if (m_controlBlock != nullptr) {
                m_controlBlock->incrementWeakCounter();
            }
        }
        return *this;
    }

    int use_count() {
        if (m_controlBlock != nullptr) {
            return m_controlBlock->refCount;
        }
        return 0;
    }

    SharedPointer<T> lock() {
        if (m_controlBlock != nullptr) {
            if (m_controlBlock->incrementCounter()) {
                return SharedPointer<T>(m_controlBlock->ptr);
            }
            return SharedPointer<T>();
        }
        else {
            return SharedPointer<T>();
        }
    }

    ~WeakPointer() {
        if (m_controlBlock != nullptr) {
            m_controlBlock->decrementWeakCounter();
        }
    }
};


int main() {
    {
        std::shared_ptr<Entity> e = std::make_shared<Entity>();
        {
            std::shared_ptr<A> a = std::make_shared<A>();
            e->m_shared = a;
            a->m_shared = e;
            std::cout << e->m_shared.use_count() << std::endl;
        }
        std::cout << e.use_count() << std::endl;
        std::cout << e->m_shared.use_count() << std::endl;
    }
    std::cout << "hello" << std::endl;
}