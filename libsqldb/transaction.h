#pragma once
#if 0 // TODO later
#include "rack.h" // Для доступу до SqlDB
#include <iostream>

namespace ky {

/**
 * @class TransactionGuard
 * @brief RAII-обгортка для керування транзакціями бази даних.
 *
 * Автоматично починає транзакцію при створенні і відкочує її при знищенні,
 * якщо вона не була явно підтверджена за допомогою методу commit().
 *
 * Приклад використання:
 * @code
 * try {
 *     TransactionGuard tx(db);
 *     db.execute("UPDATE accounts SET balance = balance - 100 WHERE id = 1;");
 *     db.execute("UPDATE accounts SET balance = balance + 100 WHERE id = 2;");
 *     tx.commit(); // Підтверджуємо транзакцію
 * } catch (const std::exception& e) {
 *     // Транзакція буде автоматично відкочена завдяки деструктору TransactionGuard
 *     std::cerr << "Transaction failed: " << e.what() << std::endl;
 * }
 * @endcode
 */
class TransactionGuard {
public:
    /**
     * @brief Конструктор, що починає транзакцію.
     * @param db Посилання на об'єкт доступу до БД.
     */
    explicit TransactionGuard(SqlDB& db) : db_(db), committed_(false) {
        db_.beginTransaction();
    }

    /**
     * @brief Деструктор.
     *
     * Якщо транзакція не була підтверджена через commit(), викликає rollback().
     */
    ~TransactionGuard() noexcept {
        if (!committed_) {
            db_.rollback();
        }
    }

    // Забороняємо копіювання та переміщення, щоб уникнути подвійного керування транзакцією.
    TransactionGuard(const TransactionGuard&) = delete;
    TransactionGuard& operator=(const TransactionGuard&) = delete;
    TransactionGuard(TransactionGuard&&) = delete;
    TransactionGuard& operator=(TransactionGuard&&) = delete;

    /**
     * @brief Підтверджує транзакцію.
     *
     * Після виклику цього методу деструктор більше не буде намагатися
     * відкотити транзакцію.
     */
    void commit() {
        db_.commit();
        committed_ = true;
    }

private:
    SqlDB& db_;
    bool committed_;
};
} // namespace ky
#endif