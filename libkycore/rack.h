#pragma once
// @preserve all comments
#include <atomic>
#include <cassert>
#include <fstream>
#include <functional>
#include <iostream>  // Для std::cout
#include <limits>
#include <memory>
#include <optional>
#include <queue>
#include <sstream>  // Для парсингу атрибутів
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>
#include <any>
#include "RUIDGen.h"

namespace ky {

using sv = std::string_view;
using optsv = std::optional<std::string_view>;
using string = std::string;
using optstr = std::optional<std::string>;
using flags_t = std::unordered_set<string>;
using vector_prf = std::vector<RField*>;

using attrs_t = std::unordered_map<string, string>;
using roid_t = uint32_t; // Random Object ID based on RUIDGen

template <typename T>
inline typename T::mapped_type get_default(const T& container, const typename T::key_type& key,
                                           const typename T::mapped_type& default_val) {
  // Використовуємо .find(), щоб уникнути винятків, якщо ключ не знайдено
  auto it = container.find(key);
  if (it != container.end()) {
    return it->second;
  }
  return default_val;
}

template <typename T>
inline typename T::mapped_type get_assert(const T& container, const typename T::key_type& key) {
  // Використовуємо .find(), щоб уникнути винятків, якщо ключ не знайдено
  auto it = container.find(key);
  assert(it != container.end() && "Key must exist in the container");
  return it->second;
}

template <class T>
class namemap {
  std::unordered_map<sv, T*> map;

public:
public:
  namemap() = default;
  ~namemap() {
    for (auto const& [key, val] : map) {
      delete val;
    }
  }

  namemap(const namemap&) = delete;
  namemap& operator=(const namemap&) = delete;
  namemap(namemap&&) = default;
  namemap& operator=(namemap&&) = default;

  const T* operator[](const sv name) const { return get_assert(map, name); }
  T* get(const sv name) {
    auto it = map.find(name);
    if (it != map.end()) {
      return it->second;
    }
    T* p = new T{string{name}};
    map.emplace(p->name, p);
    return p;
  }
  void add(T* p) {
    if (!p) return;
    map.emplace(p->name, p);
  }
  const std::unordered_map<sv, T*>& get_map() const { return map; }
};

class svparts_t {
private:
  std::queue<sv> parts;

public:
  explicit svparts_t(sv name);
  sv pop();
  operator bool(){return !parts.empty();}
};

// Попередні оголошення
class Field;
class Table;
struct Layout;
struct LayoutNode;

// Псевдоніми типів
using fields_t = namemap<Field>;
using tables_t = namemap<Table>;
using nodes_t = std::vector<std::unique_ptr<LayoutNode>>;
struct Rack;
struct type_t {
  string name;
  type_t() = default;
  explicit type_t(sv name);
  ~type_t();

  // Заборона копіювання
  type_t(const type_t&) = delete;
  type_t& operator=(const type_t&) = delete;

  // Дозвіл переміщення
  type_t(type_t&&) noexcept;
  type_t& operator=(type_t&&) noexcept;

  // Методи, що перенаправляють виклики до реалізації
  bool is_ref() const;
  const Table* ref() const;
  bool validate(sv val) const;
  const string sql() const;
  const string sqlSufix() const;

  // Статичний метод для фіналізації колекції типів
  static void finalize(Rack& rack);

  struct Hidden;

private:
  Hidden* pimpl = nullptr;
};

struct Field {
  string name;
  flags_t flags{};
  attrs_t attrs{};
  type_t* type = nullptr;
  string sqlName() const { return name + type->sqlSufix(); }
};

struct Table {
  string name;
  flags_t flags{};
  attrs_t attrs{};
  fields_t fields{};
};

struct QTable;
struct QField {
  const Field* pf;
  const QTable* pqt;
  QField() = delete;
  explicit QField(const QTable* pqt, const Field* pf) : pf(pf), pqt(pqt) {
    assert(pf != nullptr && "Вказівник на Field не може бути нульовим");
    assert(pqt != nullptr && "Вказівник на QTable не може бути нульовим");
  };
  ~QField(){};
};

struct QTable {
  const Table* pt;
  const string alias;
  const QTable* ppqt;
  // pfkey є частиною кешу, тому mutable, щоб його можна було змінювати в const-методах.
  // Поле, що описує, який ключ у батька (ppqt) вказує на цю (this) таблицю.
  mutable const Field* fk_in_parent;

  QTable(const Table* pt) : pt(pt), alias("master"), ppqt(nullptr), fk_in_parent(nullptr) {
    assert(pt != nullptr && "Вказівник на Table не може бути нульовим");
  }

  QTable(const Table* pt, const QTable* ppqt, const Field* fk_in_parent)
      : pt(pt), alias(next_alias()), ppqt(ppqt), fk_in_parent(fk_in_parent) {
    assert(pt != nullptr && "Вказівник на Table не може бути нульовим");
  }
  virtual bool isMaster() const { return false; }
  virtual ~QTable() = default;

protected:
  // Метод тепер const, оскільки він змінює лише mutable-члени (кеш).
  QField* getQField(svparts_t& parts) const;

private:
  template <class T>
  using qmap = std::unordered_map<string, std::unique_ptr<T>>;

  // Метод тепер const, оскільки він працює з mutable-контейнером.
  template <class T, class... Args>
  T* get_or_create(qmap<T>& map, sv key, Args&&... args) const {
    auto [it, inserted] = map.try_emplace(string{key}, nullptr);
    if (inserted) {
      it->second = std::make_unique<T>(std::forward<Args>(args)...);
    }
    return it->second.get();
  }
  static string next_alias();
  // Контейнери кешу позначено як mutable, щоб їх можна було заповнювати "на льоту".
  mutable qmap<QTable> qtables;
  mutable qmap<QField> qfields;
};

struct QModel : QTable {
  const string& name;
  QModel(const Table* p) : QTable(p), name(p->name) {}
  QModel(sv name);
  // Метод тепер const, що дозволяє викликати його на const MTable.
  const QField* getQField(sv fullname) const;
  bool isMaster() const override { return true; }

private:
  using qmap = std::unordered_map<string, const QField*>;
  // Кеш полів для MTable також робимо mutable.
  mutable qmap qfields;
};

/// --- Структури для представлення даних у макеті, натхненні ky.proto.txt ---


/// Базова структура вузла макета

struct LayoutNode {
  string tag;
  attrs_t attrs;
  flags_t flags;
  nodes_t nodes;

  // Конструктор копіювання для обробки дочірніх вузлів
  LayoutNode(const LayoutNode& other);
  LayoutNode() = default;
  virtual ~LayoutNode() = default;  // Віртуальний деструктор для поліморфного базового класу
  // Метод clone() тепер не чисто віртуальний, щоб LayoutNode можна було створювати.
  virtual std::unique_ptr<LayoutNode> clone() const;
};

/**
 * @brief Метаданні про поле в макеті (колонку в списку, поле у формі).
 * Відповідає proto-повідомленню Frame.FField.
 */
struct LayoutField {
  uint32_t id = 0;
  string name;
//  string type;
  flags_t flags;
  attrs_t attrs;

  // Для глибокого копіювання
  std::unique_ptr<LayoutField> clone() const { return std::make_unique<LayoutField>(*this); }
};

// Похідні типи вузлів макета
struct LayoutNodeBox : LayoutNode {
  LayoutNodeBox() = default;
  LayoutNodeBox(const LayoutNodeBox& other) : LayoutNode(other) {}
  std::unique_ptr<LayoutNode> clone() const override { return std::make_unique<LayoutNodeBox>(*this); }
};

struct LayoutNodeList : LayoutNode {
//  std::unique_ptr<RecordsetParams> recordset;
  std::vector<std::unique_ptr<LayoutField>> fields;

  // Для глибокого копіювання
  LayoutNodeList() = default;
  // При клонуванні recordset не копіюється, а ініціалізується як nullptr.
  // Поля (визначення колонок) копіюються.
  LayoutNodeList(const LayoutNodeList& other);
  std::unique_ptr<LayoutNode> clone() const override { return std::make_unique<LayoutNodeList>(*this); }
};

struct LayoutNodeForm : LayoutNode {
  //std::unique_ptr<RecordParams> record;

  // Для глибокого копіювання
  LayoutNodeForm() = default;
  // При клонуванні record не копіюється, а ініціалізується як nullptr.
  LayoutNodeForm(const LayoutNodeForm& other) : LayoutNode(other) {}
  std::unique_ptr<LayoutNode> clone() const override { return std::make_unique<LayoutNodeForm>(*this); }
};

struct LayoutNodeFieldBox : LayoutNode {
  std::vector<std::unique_ptr<LayoutField>> fields;

  // Для глибокого копіювання
  LayoutNodeFieldBox() = default;
  LayoutNodeFieldBox(const LayoutNodeFieldBox& other);
  std::unique_ptr<LayoutNode> clone() const override { return std::make_unique<LayoutNodeFieldBox>(*this); }
};

// NOTE: Контейнер для макета
struct Layout {
  string name;
  int8_t pri;
  flags_t media;  // Типи медіа, для яких призначено макет (наприклад, "desktop", "tab", "phone", "web")
  flags_t usage;  // Використання макета, наприклад, "menu", "detail", "select" тощо
  flags_t flags;
  attrs_t attrs;
  // Кореневий вузол цього лейауту.
  std::unique_ptr<LayoutNode> root_node;

  // --- Конструктори та присвоєння ---

  Layout() = default;                 // Потрібен для std::make_unique у clone_layout
  explicit Layout(sv n) : name(n) {}  // Потрібен для фабрики namemap

  // Явно визначені конструктор копіювання та оператор присвоєння
  // для забезпечення глибокого копіювання `root_node`.
  Layout(const Layout& other)
      : name(other.name),
        pri(other.pri),
        media(other.media),
        usage(other.usage),
        flags(other.flags),
        attrs(other.attrs),
        root_node(other.root_node ? other.root_node->clone() : nullptr) {}

  Layout& operator=(const Layout& other) {
    if (this != &other) {
      name = other.name;
      pri = other.pri;
      media = other.media;
      usage = other.usage;
      flags = other.flags;
      attrs = other.attrs;
      root_node = other.root_node ? other.root_node->clone() : nullptr;
    }
    return *this;
  }

  // Конструктори переміщення та присвоєння залишаються за замовчуванням.
  // Вони ефективно передають право власності на `root_node`.
  Layout(Layout&&) = default;
  Layout& operator=(Layout&&) = default;
  ~Layout() = default;
};

// Перевантаження оператора == для Layout
inline bool operator==(const Layout& lhs, const Layout& rhs) {
  // Порівняння має бути узгодженим з std::hash<ky::Layout>.
  // Порівнюємо ті ж поля, що використовуються для обчислення хешу.
  return lhs.name == rhs.name && lhs.pri == rhs.pri && lhs.media == rhs.media && lhs.usage == rhs.usage;
}

// Спеціалізація std::hash для ky::Layout
// Ця спеціалізація повинна бути оголошена в глобальному просторі імен або в просторі імен std.
// Оскільки вона стосується ky::Layout, її можна розмістити в просторі імен ky,
// а потім використовувати std::hash<ky::Layout> через ADL або повне ім'я.
// Однак, для std::hash, правильним місцем є простір імен std.
// Оскільки ми не можемо відкривати std namespace в .h файлі,
// ми можемо визначити її в глобальному просторі імен або в просторі імен ky
// і покладатися на ADL, якщо це можливо, або використовувати повне ім'я.
// Найкращий спосіб - це оголосити її в глобальному просторі імен або в просторі імен std
// (якщо це дозволено політикою проекту).
// Для простоти та уникнення відкриття std::, ми можемо визначити її в глобальному просторі імен.

}  // namespace ky

namespace std {
template <>
struct hash<ky::Layout> {
  size_t operator()(const ky::Layout& l) const {
    // Допоміжна лямбда для комбінування хешів, аналогічно до boost::hash_combine
    auto hash_combine = [](size_t& seed, size_t hash_value) {
      seed ^= hash_value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    };

    size_t seed = 0;
    hash_combine(seed, std::hash<std::string>{}(l.name));
    hash_combine(seed, std::hash<int8_t>{}(l.pri));

    size_t media_hash = 0;
    for (const auto& s : l.media) {
      media_hash ^= std::hash<std::string>{}(s);
    }
    hash_combine(seed, media_hash);

    size_t usage_hash = 0;
    for (const auto& s : l.usage) {
      usage_hash ^= std::hash<std::string>{}(s);
    }
    hash_combine(seed, usage_hash);

    return seed;
  }
};
}  // namespace std

namespace ky {  // Повертаємося до простору імен ky

struct App {
  using layset_t = std::unordered_set<Layout>;
  using layvec_t = std::vector<Layout>;
  using layouts_t = std::variant<layvec_t, layset_t>;

  string name;
  attrs_t attrs{};
  layouts_t layouts{};
};

/// @brief Абстрактний базовий клас для всіх драйверів баз даних.
class SqlDB {
public:
    /// @brief Абстрактний клас для представлення результату SQL-запиту.
    class Result {
    public:
        virtual ~Result() = default;

        /// @brief Повертає кількість рядків у результаті.
        virtual int row_count() const = 0;

        /// @brief Повертає кількість колонок у результаті.
        virtual int column_count() const = 0;

        /// @brief Не Повертає назву колонки за її індексом - є rfields
        /// virtual ky::string column_name(int col) const = 0;

        /// @brief Повертає значення комірки за індексами рядка та колонки.
        /// @return Повертає string_view на дані. Якщо значення NULL, повертає порожній string_view.
        virtual ky::optsv get_value(int row, int col) const = 0;
        std::any rfields; 
    };

    virtual ~SqlDB() = default;

    /// Виконати запит, що повертає дані (SELECT).
    /// Драйвер несе відповідальність за кешування підготовлених запитів.
    /// @param sql Текст SQL-запиту з плейсхолдерами $1, $2, ...
    /// @param params Вектор параметрів для запиту.
    /// @return Повертає унікальний вказівник на об'єкт з результатом.
    virtual std::unique_ptr<Result> query(sv sql, const std::vector<string>& params) = 0;
    
    /// Виконати запит, що не повертає дані (INSERT, UPDATE, DELETE).
    /// @param sql SQL-запит.
    /// @param params Вектор параметрів.
    /// @return Повертає кількість змінених рядків.
    virtual int execute(sv sql, const std::vector<string>& params) = 0;
};

struct Rack {
  namemap<type_t> types{};
  namemap<Table> tables{};
  namemap<App> apps{};
  flags_t flags{};
  attrs_t attrs{};
  std::unique_ptr<SqlDB> sqldb;
  RUIDGen<roid_t> ruid32;

  // Попередній механізм ініціалізації qmodels несумісний зі спрощеним namemap.
  // Оскільки qmodels наразі не використовується, його ініціалізацію закоментовано.
  mutable namemap<QModel> qmodels{};

  static const Rack& get();
  string generate_sql() const;
  bool connect(sv connection_string);
  void finalize();

  void print_stats() const;
private:
  void finalize_id();
  void finalize_apps();
};

}  // namespace ky
