#pragma once

#include <cassert>
#include <cstdlib>
#include <new>
#include <utility>
#include <exception>
#include <memory>
#include <algorithm>

template <typename T>
class RawMemory {
public:
    RawMemory() = default;

    explicit RawMemory(size_t capacity)
        : buffer_(Allocate(capacity))
        , capacity_(capacity) {
    }
    
    RawMemory (const RawMemory& other) = delete;
    
    RawMemory& operator=(const RawMemory& other) = delete;
    
    RawMemory(RawMemory&& other) noexcept {
        Swap(other);
    }
    
    RawMemory& operator=(RawMemory&& rhs) noexcept {
        Swap(rhs);
        return *this;
    }

    ~RawMemory() {
        Deallocate(buffer_);
    }

    T* operator+(size_t offset) noexcept {
    // Разрешается получать адрес ячейки памяти, следующей за последним элементом массива
        assert(offset <= capacity_);
        return buffer_ + offset;
    }

    const T* operator+(size_t offset) const noexcept {
        return const_cast<RawMemory&>(*this) + offset;
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<RawMemory&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < capacity_);
        return buffer_[index];
    }

    void Swap(RawMemory& other) noexcept {
        std::swap(buffer_, other.buffer_);
        std::swap(capacity_, other.capacity_);
    }

    const T* GetAddress() const noexcept {
        return buffer_;
    }

    T* GetAddress() noexcept {
        return buffer_;
    }

    size_t Capacity() const {
        return capacity_;
    }

private:
    // Выделяет сырую память под n элементов и возвращает указатель на неё
    static T* Allocate(size_t n) {
        return n != 0 ? static_cast<T*>(operator new(n * sizeof(T))) : nullptr;
    }

    // Освобождает сырую память, выделенную ранее по адресу buf при помощи Allocate
    static void Deallocate(T* buf) noexcept {
        operator delete(buf);
    }

    T* buffer_ = nullptr;
    size_t capacity_ = 0;
}; 

template <typename T>
class Vector {
public:
    Vector() noexcept = default;
    
    explicit Vector(size_t size)
        : data_(RawMemory<T>(size))
        , size_(size) {
        std::uninitialized_value_construct_n(data_.GetAddress(), size);
    }

    Vector(const Vector& other)
        : data_(RawMemory<T>(other.size_))
        , size_(other.size_) {
        std::uninitialized_copy_n(other.data_.GetAddress(), other.size_, data_.GetAddress());
    } 
    
    Vector(Vector&& other) noexcept
        : data_(RawMemory<T>(std::move(other.data_)))
        , size_(other.size_) {
    }

    using iterator = T*;
    using const_iterator = const T*;

    iterator begin() noexcept {
        return data_.GetAddress();
    }
    
    iterator end() noexcept {
        return data_.GetAddress() + size_; //Используем арифметику указателей
    }
    
    const_iterator begin() const noexcept {
        return cbegin();
    }
    
    const_iterator end() const noexcept {
        return cend();
    }
    
    const_iterator cbegin() const noexcept {
        return data_.GetAddress();
    }
    
    const_iterator cend() const noexcept {
        return data_.GetAddress() + size_; //Используем арифметику указателей
    }
    
    Vector& operator=(Vector&& rhs) noexcept {
        if(this != &rhs) {
            Swap(rhs);
        }
        return *this;
    }
    
    Vector& operator=(const Vector& rhs) {
        if(this != &rhs) {
            if (rhs.size_ > data_.Capacity()) {
                Vector new_vector(rhs);
                Swap(new_vector);
            } else {
                size_t min_init_memory = std::min(size_, rhs.size_);
                std::copy_n(rhs.begin(), min_init_memory, begin());

                if (size_ < rhs.size_) {
                    std::uninitialized_copy_n(rhs.data_ + size_, rhs.size_ - size_, data_ + size_);
                } else if (size_ > rhs.size_) {
                    std::destroy_n(data_ + rhs.size_, size_ - rhs.size_);
                }
            }

            size_ = rhs.size_;
        }
        return *this;
    }

    void Swap(Vector& other) noexcept {
        data_.Swap(other.data_);
        std::swap(size_, other.size_);
    }

    void Reserve(size_t capacity) {
        if (data_.Capacity()>=capacity) {
            return;
        }
        //Выделяем новую память нужного кол-ва
        RawMemory<T> new_data(capacity);
        
        SafeInitializedRawMemory(data_.GetAddress(), size_, new_data.GetAddress());

        // Разрушаем элементы в data_
        std::destroy_n(data_.GetAddress(), size_);
        // Избавляемся от старой сырой памяти, обменивая её на новую
        data_.Swap(new_data);
        // При выходе из метода старая память будет возвращена в кучу
    }

    void Resize(size_t new_size) {
        Reserve(new_size);
        if (new_size <= data_.Capacity()) {
            if (new_size > size_) {
                std::uninitialized_value_construct_n(data_.GetAddress() + size_, new_size - size_);
            }
            if (new_size < size_) {
                std::destroy_n(data_.GetAddress()+new_size, size_ - new_size);
            }
        }

        if (new_size > data_.Capacity()) {
            Reserve(new_size);
            std::uninitialized_value_construct_n(data_.GetAddress() + size_, new_size - size_);
        }
        size_ = new_size;
    }

    void PushBack(const T& value) {
        EmplaceBack(value);
    }


/*  Правила склейки ссылок:
    1.T& -> T&
    2.T& && -> T&
    3.T&& & -> T&
    4.T&& && -> T&&
*/
    template<typename Type>
    void PushBack(Type&& value) {
        if (size_ < data_.Capacity()) {
            new (data_.GetAddress() + size_) T(std::forward<Type>(value));
        } else {
            RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
            new (new_data.GetAddress() + size_) T(std::forward<Type>(value));

            SafeInitializedRawMemory(data_.GetAddress(), size_, new_data.GetAddress());

            std::destroy_n(data_.GetAddress(), size_);
            data_.Swap(new_data);
        }
        ++size_;
    }

    void PopBack()  noexcept {
    //Обращаемся к последнему элементу памяти и удаляем (этот) 1 элемент
        std::destroy_n(data_.GetAddress()+size_-1, 1);
        --size_;
    }
    
    template <typename... Args>
	T& EmplaceBack(Args&&... args) {
        return *Emplace(end(), std::forward<Args>(args)...);
    }
    
    /* The behavior is undefined if d_last is within [first, last).
    std::move must be used instead of std::move_backward in that case.
    <---  std::move
    std::move_backward --->*/
    template <typename... Args>
    iterator Emplace(const_iterator pos, Args&&... args) {
        if (Capacity() > size_) {
            return EmplaceWithoutReallocate(pos, std::forward<Args>(args)...);
        } else {
            return EmplaceWithReallocate(pos, std::forward<Args>(args)...);
        }
    }

    iterator Erase(const_iterator pos) noexcept(std::is_nothrow_move_assignable_v<T>) {
        int index = pos - begin();
        std::move(begin() + index + 1, end(), begin()+index);
        std::destroy_at(end() - 1);
        size_--;
        return begin()+index;
    }

    iterator Insert(const_iterator pos, const T& value) {
        return Emplace(pos, value);
    }

    iterator Insert(const_iterator pos, T&& value) {
        return Emplace(pos, std::move(value));
    }

    ~Vector() {
        if(data_.GetAddress()) {
            std::destroy_n(data_.GetAddress(), size_);
        }
    }

    size_t Size() const noexcept {
        return size_;
    }

    size_t Capacity() const noexcept {
        return data_.Capacity();
    }

    const T& operator[](size_t index) const noexcept {
        return const_cast<Vector&>(*this)[index];
    }

    T& operator[](size_t index) noexcept {
        assert(index < size_);
        return data_[index];
    }

private:
    template<typename... Args>
    iterator EmplaceWithoutReallocate(const_iterator pos, Args&&... args) {
        int index = pos - begin();
        if (pos != end()) {
            new (end()) T(std::forward<T>(data_[size_ - 1]));
            std::move_backward(begin() + index, end() - 1, end());
            *(begin() + index) = T(std::forward<Args>(args)...);
        } else {
            new (end()) T(std::forward<Args>(args)...);
        }
        size_++;
        return begin() + index;
    }

    template<typename... Args>
    iterator EmplaceWithReallocate(const_iterator pos, Args&&... args) {
        int index = pos - begin();
        RawMemory<T> new_data(size_ == 0 ? 1 : size_ * 2);
        new (new_data.GetAddress() + index) T(std::forward<Args>(args)...);
        //Инициализируем новую память до нового элемента
        SafeInitializedRawMemory(data_.GetAddress(), index, new_data.GetAddress());
        //Инициализируем новую память после нового элемента
        SafeInitializedRawMemory(data_.GetAddress() + index, size_ - index, new_data.GetAddress() + index + 1);

        std::destroy_n(data_.GetAddress(), size_);
        data_.Swap(new_data);
        size_++;
        return begin() + index;
    }


    void SafeInitializedRawMemory(T* source_range, size_t count, T* raw_memory) {
    //Важно использовать constexpr для определения ветки на этапе компиляции
        if constexpr (std::is_nothrow_move_constructible_v<T> || !std::is_copy_constructible_v<T>) {
        // Перемещаем элементы в new_data, перемещая их из data_, если у типа T есть констр. перемещения
            std::uninitialized_move_n(source_range, count, raw_memory);
        } else {
        //Копируем элементы в new_data, копируя их из data_, если у типа T нет констр. перемещения
            std::uninitialized_copy_n(source_range, count, raw_memory);
        }
    }

    // Вызывает деструкторы n объектов массива по адресу buf
    static void DestroyN(T* buf, size_t n) noexcept {
        for (size_t i = 0; i != n; ++i) {
            Destroy(buf + i);
        }
    }

    // Создаёт копию объекта elem в сырой памяти по адресу buf
    static void CopyConstruct(T* buf, const T& elem) {
        new (buf) T(elem);
    }

    // Вызывает деструктор объекта по адресу buf
    static void Destroy(T* buf) noexcept {
        buf->~T();
    }

    RawMemory<T> data_;
    size_t size_ = 0;
};