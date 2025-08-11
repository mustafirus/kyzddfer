#pragma once
// @preserve all comments
#include <map>
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
  const QModel* tgtQModel = nullptr;

  /// For Recordset
  RKey() = default;

  RKey(const RField& src, const QModel& tgt) : srcRField(&src), tgtQModel(&tgt) {}
};

struct RField {
  const Record* owner;
  roid_t roid;
  const QField& qfield;
  std::unique_ptr<const RKey> rkey{nullptr};  /// Для FK-зв'язку "один-до-одного"
  const RKey* link = nullptr;                 /// Для зв'язку "один-до-багатьох"

  bool aux = false;  /// auxiliary переважно для зберігання id та *_id полів
  sv val;
  bool is_null = true;
  bool is_modified = false;
  void set(optsv from_db);
  void modify(optsv from_client);
  void flush();
  explicit RField(const Record* owner, const QField& qfield) : owner(owner), qfield(qfield) {};

private:
  std::string mval;
};

class Record {
  roid_t roid;

public:
  // Псевдоніми для типів, що використовуються в генерації SQL
  using qtusedmap_t = std::map<sv, const QTable*>;
  using vector_pqf = std::vector<const QField*>;

private:
  using RFields = std::vector<std::unique_ptr<RField>>;
  const RKey& rkey;
  RFields rfields;
  bool is_new;

protected:
  vector_prf visible_fields;
  void doLoad(const vector_prf& fields_to_load);

public:
  Record(RKey& rkey);
  const RFields& getRFields() const;

  RField& getRField(sv name);

  void New();
  void Load();
  void Refresh();
  void Save();
  void Delete();
  void Undo();
  void SetField(RField& rfield, const sv value);
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
  // Останній згенерований SQL-запит. Скидається при зміні фільтрів, сортування тощо.
  std::string last_used_sql;
  int32_t active_record_id;                         // DB id(value of field id) of active record
  std::unordered_set<int32_t> selected_record_ids;  // DB id of selected records
  // Зберігає список полів, що використовувались в останньому запиті Load().
  // Це потрібно для коректної роботи методу next().
  vector_prf fields_in_last_query;

  // Params
  std::vector<Filter> filters;
  std::vector<Sort> sorts;
  Pager pager;
  // ***

protected:
  void doLoad(const vector_prf& fields_to_load);

public:
  Recordset(QModel& qmodel);
  const RKey& getRKey() const;

  void Load();
  void Delete();
  void SetFilter(RField& rfield, const sv value);
  void SetSort(RField& rfield, Sort::Direction dir);
  void AddSort(RField& rfield, Sort::Direction dir);
  void SetPage(Pager pager);
  void SetCurrentRow(uint32_t row_page_idx);

  std::unique_ptr<SqlDB::Result> res;  // Зберігає результат запиту для ітерації курсором
  int cursor_idx_for_next = -1;  // Індекс поточного рядка курсора (-1 = перед першим)
  bool next();
  friend class SqlGenius;
};

}  // namespace ky
