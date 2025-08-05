#pragma once

#include <chrono> // For std::chrono::high_resolution_clock in example
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread> // For std::thread in example
#include <unordered_set>
#include <type_traits> // For std::is_same_v
#include <vector> // For std::vector in example

// --- Заглушка для uint128_t ---
// Захищаємо від перевизначення, якщо компілятор вже має uint128_t як розширення
#ifndef UINT128_T_DEFINED
#define UINT128_T_DEFINED
struct uint128_t {}; // Мінімальна структура, яка слугує типом-ідентифікатором
#endif

// --- Допоміжні структури для генерації випадкових значень ---
// Ці структури будуть "двигунами" генерації для різних комбінацій (T, B)

// Основний шаблон (за замовчуванням видає помилку, якщо немає спеціалізації)
template <typename T, typename B>
struct RandomValueGenerator {
    T operator()(std::mt19937_64& rng) const {
        throw std::logic_error("Unsupported combination of token type and generator type.");
    }
};

// Спеціалізація для uint32_t (T=uint32_t, B=uint32_t)
template <>
struct RandomValueGenerator<uint32_t, uint32_t> {
    uint32_t operator()(std::mt19937_64& rng) const {
        std::uniform_int_distribution<uint32_t> dist;
        return dist(rng);
    }
};

// Спеціалізація для uint64_t (T=uint64_t, B=uint64_t)
template <>
struct RandomValueGenerator<uint64_t, uint64_t> {
    uint64_t operator()(std::mt19937_64& rng) const {
        std::uniform_int_distribution<uint64_t> dist;
        return dist(rng);
    }
};

// Спеціалізація для std::string, що генерується з uint32_t (8 шістнадцяткових символів, 32 біти)
template <>
struct RandomValueGenerator<std::string, uint32_t> {
    std::string operator()(std::mt19937_64& rng) const {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        std::uniform_int_distribution<uint32_t> dist;
        ss << std::setw(8) << dist(rng);
        return ss.str();
    }
};

// Спеціалізація для std::string, що генерується з uint64_t (16 шістнадцяткових символів, 64 біти)
template <>
struct RandomValueGenerator<std::string, uint64_t> {
    std::string operator()(std::mt19937_64& rng) const {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        std::uniform_int_distribution<uint64_t> dist;
        ss << std::setw(16) << dist(rng);
        return ss.str();
    }
};

// Спеціалізація для std::string, що генерується з uint128_t (32 шістнадцяткових символів, 128 біт)
template <>
struct RandomValueGenerator<std::string, uint128_t> {
    std::string operator()(std::mt19937_64& rng) const {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        std::uniform_int_distribution<uint64_t> dist; // Використовуємо uint64_t для генерації двох частин
        ss << std::setw(16) << dist(rng); // Перші 64 біти
        ss << std::setw(16) << dist(rng); // Другі 64 біти (всього 128)
        return ss.str();
    }
};

// ---
// class RUIDGen
// T - тип ідентифікатора (наприклад, uint32_t, std::string)
// B - тип, що визначає "бітність" (наприклад, uint32_t, uint64_t, uint128_t)
// ---
template <typename T, typename B = T>
class RUIDGen {
public:
    // Перевірки на етапі компіляції для допустимих комбінацій T і B
    static_assert(
        (std::is_same_v<T, uint32_t> && std::is_same_v<B, uint32_t>) ||
        (std::is_same_v<T, uint64_t> && std::is_same_v<B, uint64_t>) ||
        (std::is_same_v<T, std::string> && (std::is_same_v<B, uint32_t> || std::is_same_v<B, uint64_t> || std::is_same_v<B, uint128_t>)),
        "Unsupported combination of ID type and generator source type (B).");

    explicit RUIDGen() : rng(random_device()) {}

    // Оператор виклику (функтор), який генерує та видає унікальний ідентифікатор
    T operator()() {
        T value;
        bool unique = false;
        std::lock_guard<std::mutex> lock(mtx);

        RandomValueGenerator<T, B> generator; // Інстанціюємо правильний генератор
        for (int i = 0; i < 100; ++i) { // До 100 спроб для уникнення колізій (вкрай малоймовірно)
            value = generator(rng); // Викликаємо його оператор() з rng
            if (issuedValues.find(value) == issuedValues.end()) {
                issuedValues.insert(value);
                unique = true;
                break;
            }
        }

        if (!unique) {
            throw std::runtime_error("Failed to generate a unique value after multiple attempts.");
        }
        return value;
    }

    // Метод для перевірки, чи було значення вже видане
    bool isIssued(const T& value) const {
        std::lock_guard<std::mutex> lock(mtx);
        return issuedValues.count(value) > 0;
    }

    // Метод для відкликання (видалення) значення
    bool revoke(const T& value) {
        std::lock_guard<std::mutex> lock(mtx);
        return issuedValues.erase(value) > 0;
    }

    // Метод для отримання кількості виданих значень
    size_t getIssuedCount() const {
        std::lock_guard<std::mutex> lock(mtx);
        return issuedValues.size();
    }

private:
    std::random_device random_device;
    std::mt19937_64 rng;
    std::unordered_set<T> issuedValues;
    mutable std::mutex mtx;
};

