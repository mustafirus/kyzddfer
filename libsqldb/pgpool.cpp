#include "pgpool.h"
#include <iostream>
#include <algorithm>
#include <iomanip>

// --- Реалізація PgConn ---

int PgConn::arc_hard_limit = 15;

PgConn::PgConn(const std::string& connInfo) : cache(arc_hard_limit) {
    conn = PQconnectdb(connInfo.c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        throw std::runtime_error("Connection failed: " + std::string(PQerrorMessage(conn)));
    }
}

PgConn::~PgConn() {
    if (conn) {
        PQfinish(conn);
    }
}


// --- Реалізація PgPool ---

PgPool::PgPool(std::string connInfo, size_t hardLimit, 
           std::chrono::seconds growthTimeout, std::chrono::seconds idleTimeout)
    : connInfo(std::move(connInfo)),
      hardLimit(hardLimit),
      growthTimeout(growthTimeout),
      idleTimeout(idleTimeout)
{
    // Створюємо 1 бандл на старті
    auto initial_bundle = std::make_unique<PgConn>(this->connInfo);
    available.push_back(initial_bundle.get());
    storage.push_back(std::move(initial_bundle));
}

PgPool::~PgPool() = default;

void PgPool::release(PgConn* pgConn) {
    std::lock_guard<std::mutex> lock(mutex);
    pgConn->last_released_time = std::chrono::steady_clock::now();
    available.push_back(pgConn);
    cv.notify_one();
}

PgConn* PgPool::acquire() {
    std::unique_lock<std::mutex> lock(mutex);

    // --- Логіка скорочення ---
    if (!available.empty()) {
        auto now = std::chrono::steady_clock::now();
        
        // Ітеруємо storage у зворотному порядку (від найновіших до найстаріших)
        for (int i = storage.size() - 1; i >= 0; --i) {
            if (storage.size() <= minLimit) {
                break; 
            }
            PgConn* bundle_to_check = storage[i].get();
            auto available_it = std::find(available.begin(), available.end(), bundle_to_check);

            if (available_it != available.end()) {
                // Бандл вільний. Перевіряємо його таймаут.
                if (now - bundle_to_check->last_released_time > idleTimeout) {
                    // Цей бандл прострочений. Видаляємо його.
                    std::cout << "[PgPool] Pruning newest idle connection." << std::endl;
                    available.erase(available_it);
                    storage.erase(storage.begin() + i);
                } else {
                    // Знайшли найновіший вільний бандл, який ще не прострочений.
                    // Припиняємо будь-яке подальше скорочення.
                    break; 
                }
            }
        }
    }

    // --- Логіка отримання/зростання ---
    while(true) { 
        if (!available.empty()) {
            PgConn* pgConn = available.front();
            available.pop_front();
            return pgConn;
        }

        if (cv.wait_for(lock, growthTimeout) == std::cv_status::timeout) {
            if (storage.size() < hardLimit) {
                std::cout << "[PgPool] No available connections. Creating new." << std::endl;
                auto new_bundle = std::make_unique<PgConn>(connInfo);
                PgConn* raw_ptr = new_bundle.get();
                storage.push_back(std::move(new_bundle));
                print_stats_unlocked();
                return raw_ptr;
            } else {
                 std::cout << "[PgPool] Timeout, but hard limit reached. Waiting again." << std::endl;
            }
        }
    }
}

// Нова приватна функція, що не блокує м'ютекс
void PgPool::print_stats_unlocked() const {
    std::cout << "[Stats] Total: " << storage.size()
              << " | Available: " << available.size()
              << std::endl;
}

// Публічна функція, як і раніше, блокує м'ютекс
void PgPool::print_stats() const {
    std::lock_guard<std::mutex> lock(mutex);
    print_stats_unlocked();
}

// --- Реалізація PgPoolRaii ---
PgPoolRaii::PgPoolRaii(PgPool& pool) : pool(pool), pgConn(pool.acquire()) {}

PgPoolRaii::~PgPoolRaii() {
    if (pgConn) {
        pool.release(pgConn);
    }
}
