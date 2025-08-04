#include "rack.h"

// Підключаємо заголовки всіх реалізацій драйверів
#include "sqldrvpg.h"
// #include "sqldrvmy.h"
// #include "sqldrvlite.h"
// #include "sqldrvodbc.h"

namespace ky {

bool Rack::connect(sv connection_string) {
    sv scheme;
    if (auto pos = connection_string.find("://"); pos != sv::npos) {
        scheme = connection_string.substr(0, pos);
    } else {
        // Обробка помилки: невірна строка підключення
        return false;
    }

    if (scheme == "pgsql" || scheme == "postgresql") {
        this->sqldb = std::make_unique<SqlDrvPg>(connection_string);
    } else {
        // Інші драйвери...
        // Обробка помилки: непідтримувана схема
        return false;
    }

    return true; // або результат реального підключення
}

} // namespace ky