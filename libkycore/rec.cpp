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

void RField::setId(sv new_id) const {
  // Цей метод є const, але може змінювати mutable члени
  is_null = new_id.empty();
  if (is_null) {
    val = sv{};
    mval.clear();
  } else {
    mval = new_id;
    val = mval;
  }
}

void RField::flush() {
  is_modified = false;
  is_null = true;
  val = sv{};
  mval.clear();
}

// --- Record ---

Record::Record(const RKey& rkey) : rkey(rkey) {}

//const Record::RFields& Record::getRFields() const { return rfields; }

RField& Record::getRField(sv name) {
  const QField* pqf = rkey.tgtQModel->getQField(name);

  for (const auto& rf_ptr : rfields) {
    if (&rf_ptr->qfield == pqf) {
      return *rf_ptr;
    }
  }

  RField& rf = *rfields.emplace_back(std::make_unique<RField>(RField{this, *pqf}));
  auto t = pqf->pf->type;
  if (pqf->pqt->isMaster() && t->is_ref()) {
    // NOTE: qmodels - dynamycaly updates
    const QModel* p_qmodel = Rack::get().qmodels.get(t->ref()->name);
    assert(p_qmodel != nullptr);
    rf.rkey = std::make_unique<RKey>(rf, *p_qmodel);
  }
  return rf;
}

RField* Record::getRField(const QTable* pqt, const Field* pf) {
  for (const auto& rf_ptr : rfields) {
    const auto& qfield = rf_ptr->qfield;
    if (qfield.pqt == pqt && qfield.pf == pf) {
      return rf_ptr.get();
    }
  }
  return nullptr;
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
      // **ВИПРАВЛЕНО:** Використовуємо rkey для встановлення ID
      rkey.srcRField->setId(res->get_value(0, 0).value_or(""));
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

Recordset::Recordset(const QModel& qmodel) : Record(rkey) {
  rkey.tgtQModel = &qmodel;
  rkey.srcRField = &getRField("id");
}

Recordset::Recordset(RField& lookupRField_ref) : Recordset(*lookupRField_ref.rkey->tgtQModel) {
  lookupRField = &lookupRField_ref;
  /// UNIMPLEMENTED Поки не Перевіряємо, чи є для цього поля вбудований фільтр у метаданих.
}

Recordset::Recordset(const QModel& qmodel, const RKey& parentRKey, sv refFieldName) : Recordset(qmodel) {
  rlink = &getRField(refFieldName);
  rlink->link = &parentRKey;
}

//const RKey& Recordset::getRKey() const { return rkey; }

// rec.cpp

void Recordset::doLoad(const vector_prf& fields_to_load) {
  SqlGenius genius(this);
  auto& db = Rack::get().sqldb;

  // --- КРОК 1: Завжди отримуємо актуальну загальну кількість записів ---
  if (!countSqlCache) {
    // Генеруємо SQL для COUNT, тільки якщо він не був кешований
    countSqlCache = genius.gen_select_count();
  }

  if (countSqlCache && !countSqlCache->empty()) {
    auto count_params = genius.getOrderedParams(*countSqlCache);
    std::unique_ptr<SqlDB::Result> count_res = db->query(*countSqlCache, count_params);
    if (count_res && count_res->row_count() > 0) {
      this->total_count = std::stoi(std::string(count_res->get_value(0, 0).value()));
    } else {
      this->total_count = 0;
    }
  }

  // --- КРОК 2: Завантажуємо ID для поточної сторінки (лише за потреби) ---
  if (!pageCursorIds) {
    if (!idsSqlCache) {
      // Генеруємо SQL для SELECT id, тільки якщо він не був кешований
      idsSqlCache = genius.gen_select_ids();
    }

    // Ініціалізуємо вектор, навіть якщо запит нічого не поверне
    pageCursorIds.emplace();

    if (idsSqlCache && !idsSqlCache->empty()) {
      auto ids_params = genius.getOrderedParams(*idsSqlCache);
      std::unique_ptr<SqlDB::Result> ids_res = db->query(*idsSqlCache, ids_params);

      if (ids_res && ids_res->row_count() > 0) {
        pageCursorIds->reserve(ids_res->row_count());
        for (int i = 0; i < ids_res->row_count(); ++i) {
          // **ВИПРАВЛЕНО:** Тепер `pageCursorIds` є `vector<string>`, тому `stoi` не потрібен.
          pageCursorIds->push_back(std::string(ids_res->get_value(i, 0).value()));
        }
      }
    }
  }

  // --- КРОК 3: Завантажуємо повні дані для ID поточної сторінки ---

  // Очищуємо старий тимчасовий результат
  res.reset();

  if (pageCursorIds && !pageCursorIds->empty()) {
    // **ВИПРАВЛЕНО:** Конвертація ID в рядки більше не потрібна.
    std::string data_sql = genius.gen_select_by_ids(fields_to_load, *pageCursorIds);

    if (!data_sql.empty()) {
      auto data_params = genius.getOrderedParams(data_sql);
      // Використовуємо query_once, щоб не засмічувати кеш
      res = db->query_once(data_sql, data_params);
    }
  }

  // Зберігаємо список полів, з якими був зроблений запит, для методу next()
  this->fields_in_last_query = fields_to_load;

  // Скидаємо курсор перед першим записом
  cursor_idx_for_next = -1;
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
      ids_to_delete.push_back(id);
    }
  } else if (!rkey.srcRField->is_null) {
    // **ВИПРАВЛЕНО:** Отримуємо ID активного запису з `rkey`.
    ids_to_delete.push_back(std::string(rkey.srcRField->val));
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

// rec.cpp (доповнення)

void Recordset::SetFilter(RField& rfield, sv value) {
  // Перевірка, що поле належить цьому Recordset'у
  assert(rfield.owner == this && "Attempted to set a filter using an RField from a different owner!");

  // Патерн "знайти та оновити, або додати новий"
  auto it = std::find_if(filters.begin(), filters.end(), [&](const Filter& f) { return &f.rfield == &rfield; });

  if (it != filters.end()) {
    // Фільтр для цього поля вже існує, оновлюємо його значення
    it->value = string(value);
  } else {
    // Додаємо новий фільтр
    filters.push_back({rfield, string(value)});
  }

  // Зміна фільтра робить неактуальними і SQL, і список ID
  countSqlCache.reset();
  idsSqlCache.reset();
  pageCursorIds.reset();

  // Завжди повертаємо користувача на першу сторінку після зміни фільтра
  pager.offset = 0;
}

void Recordset::SetSort(RField& rfield, Sort::Direction dir) {
  assert(rfield.owner == this && "Attempted to set a sort using an RField from a different owner!");

  // SetSort повністю замінює поточне сортування
  sorts.clear();
  sorts.push_back({rfield, dir});

  // Зміна сортування робить неактуальними і SQL, і список ID
  countSqlCache.reset();
  idsSqlCache.reset();
  pageCursorIds.reset();

  // Завжди повертаємо користувача на першу сторінку
  pager.offset = 0;
}

void Recordset::SetPage(Pager newPager) {
  // Оновлюємо параметри пагінації
  this->pager = newPager;

  // SQL-запити залишаються валідними, але список ID для старої сторінки вже неактуальний.
  // Скидаємо його, щоб при наступному Load() завантажились ID для нової сторінки.
  this->pageCursorIds.reset();
}

// Реалізація AddSort, як обговорювалось
void Recordset::AddSort(RField& rfield, Sort::Direction dir) {
  assert(rfield.owner == this && "Attempted to add a sort using an RField from a different owner!");

  // Додаємо нове поле сортування до існуючих
  sorts.push_back({rfield, dir});

  // Логіка аналогічна SetSort
  countSqlCache.reset();
  idsSqlCache.reset();
  pageCursorIds.reset();
  pager.offset = 0;
}
void Recordset::SetCurrentRow(uint32_t row_page_idx) {
  assert(rkey.srcRField && "rkey.srcRField is not initialized in Recordset constructor!");

  if (pageCursorIds && row_page_idx < pageCursorIds->size()) {
    const string& new_active_id = (*pageCursorIds)[row_page_idx];

    // Використовуємо новий, семантично чистий const метод.
    // Це сигналізує, що ми не змінюємо логічний стан Recordset,
    // а лише пересуваємо внутрішній "повзунок".
    rkey.srcRField->setId(new_active_id);
  } else {
    rkey.srcRField->setId(sv{});
  }
}

// Метод застосування вибору
void Recordset::ApplySelection() {
  // Якщо вказівник на поле-джерело існує, значить,
  // цей Recordset використовується для вибору.
  if (lookupRField) {
    // Беремо значення ID з поточного рядка...
    sv selectedId = this->rkey.srcRField->val;
    // ...і напряму модифікуємо поле-джерело.
    lookupRField->modify(selectedId);
  }
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
  assert(res->column_count() == fields_in_last_query.size() && "Mismatch between data columns and RField pointers");

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
