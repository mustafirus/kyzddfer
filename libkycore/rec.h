#pragma once
// @preserve all comments
#include <map>
#include <optional>
#include <string_view>
#include <vector>

#include "rack.h"

namespace ky {

class RField;
class Record;
class Recordset;

struct RKey {
  /// For Link
  const RField* srcRField = nullptr;
  QModel* tgtQModel = nullptr;

  /// For Recordset
  RKey() = default;

  RKey(const RField& src, QModel& tgt) : srcRField(&src), tgtQModel(&tgt) {}
};

struct RField {
  const Record* owner;
  roid_t roid;
  const QField& qfield;
  std::unique_ptr<RKey> rkey{nullptr};  /// Для FK-зв'язку "один-до-одного"
  RKey* link = nullptr;                 /// Для зв'язку "один-до-багатьох"

  bool aux = false;  /// auxiliary переважно для зберігання id та *_id полів
  mutable sv val;
  mutable bool is_null = true;
  bool is_modified = false;
  void set(optsv from_db);
  void modify(optsv from_client);
  void setId(sv new_id) const;
  void flush();
  explicit RField(const Record* owner, const QField& qfield) : owner(owner), qfield(qfield){};

private:
  mutable std::string mval;
};

class Record {
  roid_t roid;

public:
  // Псевдоніми для типів, що використовуються в генерації SQL
  using qtusedmap_t = std::map<sv, const QTable*>;
  using vector_pqf = std::vector<const QField*>;

  const RKey& rkey;
private:
  using RFields = std::vector<std::unique_ptr<RField>>;
  RFields rfields;
  bool is_new;

protected:
  vector_prf visible_fields;
  void doLoad(const vector_prf& fields_to_load);

public:
  Record(const RKey& rkey);
  //  const RFields& getRFields() const;

  RField& getRField(sv name);

  /**
   * @brief Знаходить існуючий RField за прямими вказівниками на QTable та Field.
   * @details Цей метод виконує лише пошук серед вже ініціалізованих полів.
   * Він не створює новий RField, якщо його не знайдено.
   * @param pqt Вказівник на кваліфіковану таблицю (QTable).
   * @param pf Вказівник на метадані поля (Field).
   * @return Вказівник на знайдений RField або `nullptr`, якщо поле не знайдено.
   */
  RField* getRField(const QTable* pqt, const Field* pf);

  void New();
  void Load();
  void Refresh();
  void Save();
  void Delete();
  void Undo();
  void SetVisibleFields(const vector_prf& fields);
  friend class SqlGenius;
  virtual ~Record() = default;
};

class Recordset : public Record {
public:
  struct Filter {
    RField& rfield;
    string value;
  };

  struct Sort {
    enum class Direction { NONE, ASC, DESC };
    RField& rfield;
    Direction dir = Direction::NONE;
  };

  struct Pager {
    uint32_t offset = 0;
    uint32_t limit = 30;  // Типовий розмір сторінки
  };
  using URecord = std::unique_ptr<Record>;

private:
  // *** Members ***
  RKey rkey;
  RField* lookupRField = nullptr;
  RField* rlink = nullptr;

  std::unordered_set<string> selected_record_ids;  // DB id of selected records
  uint32_t total_count = 0;

  // Кеші для SQL запитів, що не залежать від сторінки
  std::optional<std::string> countSqlCache;
  std::optional<std::string> idsSqlCache;

  // Кеш ID записів для поточної завантаженої сторінки
  std::optional<std::vector<string>> pageCursorIds;

  // Params
  std::vector<Filter> filters;
  std::vector<Sort> sorts;
  Pager pager;
  // ***

  // Зберігає список полів, що використовувались в останньому запиті Load().
  // Це потрібно для коректної роботи методу next().
  // (Пропозиція: перейменувати на lastQueryFields для ясності)
  vector_prf fields_in_last_query;
  std::unique_ptr<SqlDB::Result> res;  // Зберігає результат запиту для ітерації курсором
  int cursor_idx_for_next = -1;  // Індекс поточного рядка курсора (-1 = перед першим)

  void doLoad(const vector_prf& fields_to_load);

public:
  Recordset(QModel& qmodel);

  /**
   * @brief Створює динамічно пов'язаний дочірній список (Child List).
   * @details Цей конструктор встановлює постійний "живий" зв'язок
   * з батьківським RKey. Перед кожним завантаженням він буде
   * автоматично фільтруватися за актуальним значенням з батька.
   *
   * @param qmodel Модель даних для цього (дочірнього) Recordset'а.
   * @param parentRKey Ключ батьківського запису, що є джерелом значення.
   * @param refFieldName Назва поля зовнішнього ключа в цій (дочірній) таблиці.
   */
  explicit Recordset(QModel& qmodel, RKey& parentRKey, sv refFieldName);

  /**
   * @brief Створює пов'язаний Recordset (для Lookup).
   * ### Сценарій 2: Lookup (Вибір значення)
   * @param lookupRField RField, що ініціював створення цього списку.
   */
  explicit Recordset(RField& lookupRField);

//  const RKey& getRKey() const;

  void Load();
  void Delete();
  void SetFilter(RField& rfield, const sv value);
  void SetSort(RField& rfield, Sort::Direction dir);
  void AddSort(RField& rfield, Sort::Direction dir);
  void SetPage(Pager pager);
  void SetCurrentRow(uint32_t row_page_idx);

  // Метод для застосування вибору і повернення значення
  void ApplySelection();

  bool next();
  friend class SqlGenius;
};

}  // namespace ky
