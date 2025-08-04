#include "sqldrvpg.h"
#include <iostream>
#include <stdexcept>

namespace ky {

// --- SqlDrvPg::Result ---

SqlDrvPg::Result::Result(PGresult* res) : res(res) {}

SqlDrvPg::Result::~Result() {
    if (res) PQclear(res);
}

int SqlDrvPg::Result::row_count() const {
    return PQntuples(res);
}

int SqlDrvPg::Result::column_count() const {
    return PQnfields(res);
}
/* 
string SqlDrvPg::Result::column_name(int col) const {
    return PQfname(res, col);
}
*/
ky::optsv SqlDrvPg::Result::get_value(int row, int col) const {
    if (PQgetisnull(res, row, col)) {
        // Якщо значення в базі NULL, повертаємо пустий optional
        return std::nullopt; // або просто {}
    }
    // Інакше, повертаємо string_view, загорнутий в optional
    return ky::optsv(sv(PQgetvalue(res, row, col), PQgetlength(res, row, col)));
}

// --- SqlDrvPg ---

SqlDrvPg::SqlDrvPg(sv connection_string)
    // Ініціалізуємо пул напряму в списку ініціалізації.
    // TODO: параметри пулу (hardLimit, timeouts) слід винести в конфігурацію.
    : pool(string(connection_string), 10, std::chrono::seconds(5), std::chrono::seconds(60)) {}

SqlDrvPg::~SqlDrvPg() = default;

std::unique_ptr<SqlDB::Result> SqlDrvPg::query(sv sql, const std::vector<string>& params) {
    PgPoolRaii conn_guard(pool);
    PgConn* pg_conn = conn_guard.get();

    // Використовуємо кеш підготовлених запитів, що прив'язаний до конкретного з'єднання.
    PgPrepStmt* stmt = pg_conn->cache.get(string(sql));
    if (!stmt) {
        stmt = pg_conn->cache.put(string(sql), PgPrepStmt(pg_conn->conn, string(sql)));
    }

    std::vector<const char*> param_values;
    param_values.reserve(params.size());
    for (const auto& p : params) {
        param_values.push_back(p.c_str());
    }

    PGresult* res = PQexecPrepared(pg_conn->conn, stmt->stmtName.c_str(), params.size(), param_values.data(), nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        string error_msg = PQerrorMessage(pg_conn->conn);
        PQclear(res);
        throw std::runtime_error(error_msg);
    }

    return std::make_unique<Result>(res);
}

int SqlDrvPg::execute(sv sql, const std::vector<string>& params) {
    PgPoolRaii conn_guard(pool);
    PgConn* pg_conn = conn_guard.get();

    std::vector<const char*> param_values;
    param_values.reserve(params.size());
    for (const auto& p : params) {
        param_values.push_back(p.c_str());
    }

    PGresult* res = PQexecParams(pg_conn->conn, string(sql).c_str(), params.size(), nullptr, param_values.data(), nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        string error_msg = PQerrorMessage(pg_conn->conn);
        PQclear(res);
        throw std::runtime_error(error_msg);
    }

    string tuples = PQcmdTuples(res);
    PQclear(res);
    return tuples.empty() ? 0 : std::stoi(tuples);
}

} // namespace ky
