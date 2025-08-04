// rec.cpp

#include "rec.h"

#include <any>
#include <cassert>
#include <stdexcept>

#include "SqlGenius.h"  // Підключаємо наш генератор SQL
#include "rack.h"       // Для доступу до SqlDB

namespace ky {

// --- RField ---

void RField::set(optsv from_db) {
  is_modified = false;
  mval.clear();
  is_null = !from_db.has_value();
  val = is_null ? sv{} : *from_db;
}

void RField::modify(optsv from_client) {
  is_modified = true;
  is_null = !from_client.has_value();
  if (is_null) {
    mval.clear();  // .clear() не звільняє пам'ять так шо чи воно є чи нема - байдуже
  } else {
    mval = *from_client;
  }
  val = mval;  // view тепер завжди дивиться на наш mval
}

void RField::flush() {
  is_modified = false;
  is_null = true;
  val = sv{};
  mval.clear();
}

// --- Record ---

Record::Record(RKey& rkey) : rkey(rkey) {}

const Record::RFields& Record::getRFields() const { return rfields; }

RField& Record::getRField(sv name) {
  const QField* pqf = rkey.tgtQModel->getQField(name);

  for (const auto& rf_ptr : rfields) {
    if (&rf_ptr->qfield == pqf) {
      return *rf_ptr;
    }
  }

  RField& rf = *rfields.emplace_back(std::make_unique<RField>(RField{this,*pqf}));
  auto t = pqf->pf->type;
  if (pqf->pqt->isMaster() && t->is_ref()) {
    // NOTE: qmodels - dynamycaly updates
    const QModel* p_qmodel = Rack::get().qmodels.get(t->ref()->name);
    assert(p_qmodel != nullptr);
    rf.rkey = std::make_unique<RKey>(rf, *p_qmodel);
  }
  return rf;
}

void Record::New() {
  // 1. Позначаємо запис як новий.
  is_new = true;

  // 2. Ітеруємо по всіх полях і встановлюємо значення за замовчуванням.
  for (const auto& rfield_ptr : rfields) {
    const auto& qfield = rfield_ptr->qfield;

    // Перевіряємо, чи є у поля default-значення в метаданих (в атрибутах)
    auto it = qfield.pf->attrs.find("default");
    if (it != qfield.pf->attrs.end()) {
      // Встановлюємо значення, але не позначаємо поле як is_modified,
      // оскільки це не зміна, зроблена користувачем.
      rfield_ptr->modify(it->second);
      rfield_ptr->is_modified = false;  // Важливо! Default - це не зміна користувача.
    } else {
      // Для інших полів просто скидаємо значення.
      rfield_ptr->flush();  //
    }
  }
}

void Record::doLoad(const vector_prf& fields_to_load) {
  if (fields_to_load.empty()) return;  // Нічого завантажувати

  // 1. Створюємо SqlGenius для цього запису
  SqlGenius genius(this);

  // 2. Генеруємо SQL-запит
  std::string sql = genius.gen_select_one(fields_to_load);
  if (sql.empty()) return;

  // 3. Отримуємо параметри і виконуємо запит
  auto params = genius.getOrderedParams(sql);
  auto& db = Rack::get().sqldb;  // Отримуємо доступ до об'єкта БД

  // Припускаємо, що у SqlDB є метод query, що повертає результат
  std::unique_ptr<SqlDB::Result> res = db->query(sql, params);

  // 4. Заповнюємо поля даними з відповіді
  if (res && res->row_count() > 0) {
    for (size_t i = 0; i < fields_to_load.size(); ++i) {
      optsv value_opt = res->get_value(0, i);  // Беремо дані з першого рядка
      fields_to_load[i]->set(value_opt);  // Метод RField::set() оновлює val і скидає is_modified
    }
    is_new = false;  // Якщо щось завантажили, запис вже не новий
  }
}

void Record::Load() { doLoad(this->visible_fields); }

void Record::Refresh() {
  vector_prf unmodified_fields;
  for (const auto& rfield_ptr : rfields) {
    if (!rfield_ptr->is_modified) {
      unmodified_fields.push_back(rfield_ptr.get());
    }
  }
  doLoad(unmodified_fields);
}

void Record::SetVisibleFields(const vector_prf& fields) { this->visible_fields = fields; }

void Record::Save() {
  SqlGenius genius(this);
  std::string sql;

  // 1. Визначаємо, яку операцію виконати: INSERT чи UPDATE
  if (is_new) {
    sql = genius.gen_insert();  //
  } else {
    sql = genius.gen_update();  //
  }

  if (sql.empty()) {
    // Нічого не було змінено, виходимо
    return;
  }

  // 2. Отримуємо параметри та виконуємо запит
  auto params = genius.getOrderedParams(sql);
  auto& db = Rack::get().sqldb;

  if (is_new) {
    // Для INSERT нам потрібно отримати повернутий ID
    // Припускаємо, що у SqlDB є метод, що може повернути результат
    std::unique_ptr<SqlDB::Result> res = db->query(sql, params);
    if (res && res->row_count() > 0) {
      // Оновлюємо ID нашого запису з відповіді БД
      // Ця логіка потребуватиме реалізації отримання ID з RKey
      // update_record_id(res->get_value(0, 0));
      is_new = false;
    } else {
      throw std::runtime_error("Failed to retrieve new ID after INSERT.");
    }
  } else {
    // Для UPDATE нам не потрібен результат, лише кількість змінених рядків
    db->execute(sql, params);
  }

  // 3. <<<<<<<<<<<<< Read-after-Write >>>>>>>>>>>>>
  // Перезавантажуємо стан об'єкта з БД, щоб гарантувати консистентність
  Load();
}
void Record::Delete() {
  if (is_new) {
    // Не можна видалити те, чого немає в БД
    return;
  }

  SqlGenius genius(this);
  std::string sql = genius.gen_delete();  //
  auto params = genius.getOrderedParams(sql);

  Rack::get().sqldb->execute(sql, params);

  // Після видалення можна очистити поля або позначити об'єкт як "видалений"
  // flush_fields();
}

void Record::SetField(RField& rfield, const sv value) {
  // TODO: Implement
  throw std::logic_error("Record::Set not implemented");
}

void Record::Undo() {
  if (is_new) {
    // Якщо запис ще не збережений, Undo повертає його до початкового стану.
    this->New();
  } else {
    // Якщо запис вже існує, Undo перезавантажує його з БД, відкидаючи локальні зміни.
    this->Load();
  }
}

// --- Recordset ---

Recordset::Recordset(QModel& qmodel) : Record(rkey) { rkey.tgtQModel = &qmodel; }

const RKey& Recordset::getRKey() const { return rkey; }

void Recordset::doLoad(const vector_prf& fields_to_load) {
  SqlGenius genius(this);
  auto& db = Rack::get().sqldb;

  // === КРОК 1: Отримати загальну кількість записів для пагінації ===
  std::string count_sql = genius.gen_select_count();  //
  if (!count_sql.empty()) {
    auto count_params = genius.getOrderedParams(count_sql);
    std::unique_ptr<SqlDB::Result> count_res = db->query(count_sql, count_params);
    if (count_res && count_res->row_count() > 0) {
      // Встановлюємо загальну кількість записів у пейджер
      // pager.total_items = std::stoi(count_res->get_value(0, 0).value());
    }
  }

  // === КРОК 2: Отримати ID записів для поточної сторінки ===
  std::string ids_sql = genius.gen_select_ids();  //
  if (ids_sql.empty()) {
    res.reset();  // Немає ID - немає даних
    return;
  }

  auto ids_params = genius.getOrderedParams(ids_sql);
  std::unique_ptr<SqlDB::Result> ids_res = db->query(ids_sql, ids_params);

  std::vector<std::string> ids_on_page;
  if (ids_res && ids_res->row_count() > 0) {
    for (int i = 0; i < ids_res->row_count(); ++i) {
      ids_on_page.push_back(std::string(ids_res->get_value(i, 0).value()));
    }
  } else {
    res.reset();  // Немає ID - немає даних
    return;
  }

  // === КРОК 3: Завантажити повні дані для ID поточної сторінки ===
  // Використовуємо fields_to_load, передані як аргумент
  std::string data_sql = genius.gen_select_by_ids(fields_to_load, ids_on_page);
  if (data_sql.empty()) {
    res.reset();
    return;
  }

  auto data_params = genius.getOrderedParams(data_sql);
  // Зберігаємо фінальний результат у члені класу для подальшого використання методом next()
  res = db->query(data_sql, data_params);  //

  // Зберігаємо список полів, з якими був зроблений запит.
  // Це потрібно для методу next(), щоб правильно зіставити колонки з полями.
  fields_in_last_query = fields_to_load;

  // Скидаємо курсор перед першим записом
  cursor_idx_for_next = -1;  //
}

void Recordset::Load() {
  // Просто викликаємо захищений "робочий" метод з видимими полями
  doLoad(this->visible_fields);
}

void Recordset::Delete() {
  std::vector<std::string> ids_to_delete;

  // 1. Визначаємо, що видаляти: виділені записи чи активний
  if (!selected_record_ids.empty()) {
    for (const auto& id : selected_record_ids) {
      ids_to_delete.push_back(std::to_string(id));
    }
  } else if (active_record_id > 0) {
    ids_to_delete.push_back(std::to_string(active_record_id));
  }

  if (ids_to_delete.empty()) {
    return;  // Нічого видаляти
  }

  // 2. Створюємо SqlGenius і генеруємо запит за допомогою нового методу
  SqlGenius genius(this);
  std::string sql = genius.gen_delete_by_ids(ids_to_delete);

  if (sql.empty()) return;

  // 3. Виконуємо запит
  auto params = genius.getOrderedParams(sql);
  Rack::get().sqldb->execute(sql, params);

  // 4. Після видалення обов'язково перезавантажуємо дані
  // щоб користувач побачив актуальний список.
  Load();
}

void Recordset::SetFilter(RField& rfield, sv value) {
  // TODO: Implement
  throw std::logic_error("Recordset::setFilter not implemented");
}

void Recordset::SetSort(RField& rfield, Sort::Direction dir) {
  // TODO: Implement
  throw std::logic_error("Recordset::setSort not implemented");
}

void Recordset::SetPage(int page_num) {
  // TODO: Implement
  throw std::logic_error("Recordset::setPage not implemented");
}

bool Recordset::next() {
  if (!res) {
    return false;
  }

  cursor_idx_for_next++;

  if (cursor_idx_for_next >= res->row_count()) {
    res.reset();  // Звільняємо результат
    cursor_idx_for_next = -1;
    return false;
  }

  // 1. Перевірка на консистентність: кількість колонок у результаті
  // має збігатися з кількістю полів, які ми запитували.
  assert(res->column_count() == fields_in_last_query.size() &&
         "Mismatch between data columns and RField pointers");

  // 2. Заповнюємо RFields, ітеруючи по колонках і вектору fields_in_last_query_ одночасно
  for (int j = 0; j < res->column_count(); ++j) {
    // Отримуємо прямий вказівник на RField, який треба заповнити
    RField* rfield = fields_in_last_query[j];

    if (!rfield) {
      continue;  // Пропускаємо, якщо вказівник нульовий
    }

    // Отримуємо значення з комірки (row=current_row_, col=j)
    optsv value_opt = res->get_value(cursor_idx_for_next, j);

    // Встановлюємо значення напряму. Ніяких пошуків за іменем!
    rfield->set(value_opt);
  }

  return true;
}

}  // namespace ky