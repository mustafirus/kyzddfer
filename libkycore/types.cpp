#include "rack.h"
#include <memory>
#include <stdexcept>
#include <charconv>

namespace ky {

// Псевдонім для зручності
using base_t = type_t::Hidden;

// --- Визначення прихованої ієрархії ---
struct type_t::Hidden {
  virtual ~Hidden() = default;
  virtual bool is_ref() const { return false; }
  virtual const Table *ref() const { return nullptr; }
  virtual bool validate([[maybe_unused]] sv val) const { return false; }
  virtual const string sql() const = 0;
  virtual const string sqlSufix() const { return ""; };

  static sv get_base_type(sv type_str) {
    size_t pos = type_str.find('(');
    if (pos != sv::npos)
      return type_str.substr(0, pos);
    return type_str;
  }
};

// --- Конкретні реалізації ---
struct type_id_t : base_t {
  const string sql() const override { return "serial PRIMARY KEY"; };
};

struct type_ref_t : base_t {
  const Table *ref_table = nullptr;

  explicit type_ref_t(const Table* p) : ref_table(p){}
  const string sql() const override {
    return "INT"; /* або реалізація FOREIGN KEY */
  }
  const string sqlSufix() const override { return "_id"; }
  const Table *ref() const override {
    assert(ref_table != nullptr);
    return ref_table;
  }
  bool is_ref() const override { return true; }
};

struct type_varchar_t : base_t {
  uint32_t maxlen = 0;
  const string sql() const override {
    return maxlen > 0 ? "varchar(" + std::to_string(maxlen) + ")" : "varchar";
  }
};

struct type_int_t : base_t {
  const string sql() const override { return "INT"; }
};

struct type_date_t : base_t {
  const string sql() const override { return "DATE"; };
};

struct type_text_t : base_t {
  const string sql() const override { return "TEXT"; };
};

struct type_dec_t : base_t { // temporary stub
  const string sql() const override { return ""; };
};

struct type_unknown_t : base_t { // regular unknown type
  const string sql() const override { return ""; };
};

// ... інші реалізації ...

// --- Реалізація методів type_t ---
type_t::type_t(sv name) : name(name), pimpl(nullptr) {}
type_t::~type_t(){delete pimpl;};
type_t::type_t(type_t &&) noexcept = default;
type_t &type_t::operator=(type_t &&) noexcept = default;

// --- Методи-проксі ---
bool type_t::is_ref() const {
  assert(pimpl != nullptr && "Type is not finalized.");
  return pimpl->is_ref();
}

const Table *type_t::ref() const {
  assert(pimpl != nullptr && "Type is not finalized.");
  return pimpl->ref();
}

bool type_t::validate(sv val) const {
  assert(pimpl != nullptr && "Type is not finalized.");
  return pimpl->validate(val);
}

const string type_t::sql() const {
  assert(pimpl != nullptr && "Type is not finalized.");
  return pimpl->sql();
}

const string type_t::sqlSufix() const {
  assert(pimpl != nullptr && "Type is not finalized.");
  return pimpl->sqlSufix();
}

// Допоміжна функція для парсингу визначень типів, як-от "varchar(100)"
static std::pair<sv, sv> parse_type_def(sv type_str) {
  sv base_type = type_str;
  sv args;
  size_t lparen_pos = type_str.find('(');
  if (lparen_pos != sv::npos) {
    base_type = type_str.substr(0, lparen_pos);
    size_t rparen_pos = type_str.rfind(')');
    if (rparen_pos != sv::npos && rparen_pos > lparen_pos) {
      args = type_str.substr(lparen_pos + 1, rparen_pos - lparen_pos - 1);
    }
  }
  return {base_type, args};
}

// --- Статичний метод фіналізації ---
// ПРИМІТКА: Цю функцію не можна просто перенести до finalize.cpp.
// Вона залежить від повних визначень класів-реалізацій типів (type_int_t тощо).
// Водночас деструктор type_t::~type_t(), що знаходиться у цьому ж файлі,
// також потребує цих визначень для коректної роботи `delete pimpl;`.
// Перенесення цієї функції вимагатиме перенесення і деструктора, і всіх
// деталей реалізації, що зробить рефакторинг безглуздим.
void type_t::finalize(Rack &rack) {
  //
  // УВАГА: Ця логіка має бути замінена на патерн "Фабрика" для масштабування!
  //
  for (auto const &[name, stub] : rack.types.get_map()) {
    if (stub->pimpl)
      continue; // Вже фіналізовано
    auto [base_type, args] = parse_type_def(name);
    if (base_type == "int") {
      stub->pimpl = new type_int_t();
    } else if (base_type == "id") {
      stub->pimpl = new type_id_t();
    } else if (base_type == "varchar") {
      auto p = new type_varchar_t();
      if (!args.empty()) {
        std::from_chars(args.data(), args.data() + args.size(), p->maxlen);
      }
      stub->pimpl = std::move(p);
    } else if (base_type == "ref") {
      assert(!args.empty());
      stub->pimpl = new type_ref_t(rack.tables[args]);
    } else if (base_type == "date") {
      stub->pimpl = new type_date_t();
    } else if (base_type == "text") {
      stub->pimpl = new type_text_t();
    } else {
      // Заглушка для невідомих типів, щоб уникнути падіння
      std::cerr << "Warning: Unknown type '" << name
                << "' encountered during finalization. Using a stub."
                << std::endl;
      stub->pimpl = new type_unknown_t();
    }
  }
}

/*
// ref_t
const string ref_t::sql() const {
  return "INT REFERENCES " + ref()->name + " (id)"; 
}

// varchar_t
bool varchar_t::validate(sv val) const { 
    return maxlen == 0 ? true : val.length() <= maxlen; 
}

// int_t
bool int_t::validate(sv val) const {
    if (val.empty()) { return true; } // NULL val
    int num;
    auto [ptr, ec] = std::from_chars(val.data(), val.data() + val.size(), num);
    if (ec == std::errc() && ptr == val.data() + val.size()) {
        return (num >= min && num <= max);
    }
    return false;
}

*/
} // namespace ky