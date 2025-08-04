#pragma once

#include "arc.h" // Ваш файл з реалізацією AdaptiveReplacementCache
#include <postgresql/libpq-fe.h>
#include <string>
#include <chrono>
#include <memory>
#include <vector>
#include <list>
#include <mutex>
#include <condition_variable>
#include <stdexcept>
#include <atomic>


struct PgPrepStmt {
    PGconn* conn;
    std::string stmtName;

private:
    static inline std::atomic<int> counter{0};

public:
    PgPrepStmt() : conn(nullptr) {}

    PgPrepStmt(PGconn* c, const std::string& query)
        : conn(c), stmtName("pg_prep_stmt_" + std::to_string(++counter)) {
        if (!conn) {
            throw std::invalid_argument("Connection pointer is null.");
        }
        if (query.empty()) {
            throw std::invalid_argument("Query cannot be empty.");
        }

        PGresult* res = PQprepare(conn, stmtName.c_str(), query.c_str(), 0, nullptr);

        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
             std::string errorMsg = PQerrorMessage(conn);
             PQclear(res);
             stmtName.clear();
             throw std::runtime_error("PQprepare failed for statement '" + stmtName + "': " + errorMsg);
        }

        PQclear(res);
    }

    ~PgPrepStmt() {
        if (conn && !stmtName.empty()) {
            PQclosePrepared(conn, stmtName.c_str());
        }
    }

    PgPrepStmt(const PgPrepStmt& other) = delete;
    PgPrepStmt& operator=(const PgPrepStmt& other) = delete;

    PgPrepStmt(PgPrepStmt&& other) noexcept
        : conn(other.conn), stmtName(std::move(other.stmtName)) {
        other.conn = nullptr;
        other.stmtName.clear();
    }

    PgPrepStmt& operator=(PgPrepStmt&& other) noexcept {
        if (this != &other) {
            if (conn && !stmtName.empty()) {
                PQclosePrepared(conn, stmtName.c_str());
            }

            conn = other.conn;
            stmtName = std::move(other.stmtName);

            other.conn = nullptr;
            other.stmtName.clear();
        }
        return *this;
    }
};

// Структура, що об'єднує з'єднання та його персональний кеш
struct PgConn {
    static int arc_hard_limit;

    PGconn* conn = nullptr;
    AdaptiveReplacementCache<PgPrepStmt> cache;
    std::chrono::steady_clock::time_point last_released_time;

    PgConn(const std::string& connInfo);
    ~PgConn();

    PgConn(const PgConn&) = delete;
    PgConn& operator=(const PgConn&) = delete;
};


// Клас динамічного пулу з'єднань
class PgPool {
public:
    PgPool(std::string connInfo, size_t hardLimit, 
         std::chrono::seconds growthTimeout, std::chrono::seconds idleTimeout);
    ~PgPool();

    PgPool(const PgPool&) = delete;
    PgPool& operator=(const PgPool&) = delete;

    PgConn* acquire();
    void release(PgConn* pgConn);
    
    void print_stats() const;

private:
    void print_stats_unlocked() const;


    std::string connInfo;
    size_t hardLimit;
    std::chrono::seconds growthTimeout;
    std::chrono::seconds idleTimeout;
    const size_t minLimit = 1;

    std::vector<std::unique_ptr<PgConn>> storage;
    std::list<PgConn*> available;
    
    mutable std::mutex mutex;
    std::condition_variable cv;
};


// RAII-обгортка для гарантованого повернення з'єднання в пул
class PgPoolRaii {
public:
    explicit PgPoolRaii(PgPool& pool);
    ~PgPoolRaii();

    PgPoolRaii(const PgPoolRaii&) = delete;
    PgPoolRaii& operator=(const PgPoolRaii&) = delete;

    PgConn* operator->() const { return pgConn; }
    PgConn* get() const { return pgConn; }

private:
    PgPool& pool;
    PgConn* pgConn;
};
