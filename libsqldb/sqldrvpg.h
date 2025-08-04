#pragma once

#include "rack.h" // Для доступу до базового класу SqlDB
#include "pgpool.h" // Для прямого включення PgPool

namespace ky {

class SqlDrvPg : public SqlDB {
public:
    /**
     * @param connection_string Рядок для підключення до бази даних.
     */
    explicit SqlDrvPg(sv connection_string);
    ~SqlDrvPg() override;

    std::unique_ptr<SqlDB::Result> query(sv sql, const std::vector<string>& params) override;
    int execute(sv sql, const std::vector<string>& params) override;

private:
    class Result : public SqlDB::Result {
    public:
        explicit Result(PGresult* res);
        ~Result() override;

        // Реалізація віртуальних методів
        int row_count() const override;
        int column_count() const override;
        //string column_name(int col) const override;
        optsv get_value(int row, int col) const override;
    private:
        PGresult* res;
    };

    // Пул з'єднань як прямий член класу.
    PgPool pool;
};

} // namespace ky
