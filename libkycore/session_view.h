#pragma once

#include <cassert>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "rack.h"  // Для доступу до ky::roid_t та інших базових типів
#include "rec.h"   // Для доступу до ky::Record та ky::Recordset

namespace ky {

// Попередні оголошення
class Session;
class View;

class View {
public:
using node2dto_t = const std::map<const LayoutNode*, void*>;
  // --- Статичні методи для управління DTO ---
  // Реалізація цих методів знаходиться в іншому модулі.
  static void* makeDto(const Layout& layout, node2dto_t& node_to_dto_map);
  static void killDto(void* dto);

  // --- Основний інтерфейс класу ---
  //View(Session&, const Layout&);
  /// для виклика з меню та вкладених View
  View(Session& session, View* prev, const Layout&, const RKey* rkey);
  ~View();
  View(const View&) = delete;
  View& operator=(const View&) = delete;

  void close();

private:
    struct Builder; 
    friend struct Builder;   // Надаємо Builder-у доступ до приватних полів

  Session& session;
//  const Layout& layout;
  View* prev = nullptr;
  std::map<roid_t, std::unique_ptr<Record>> records;
  void* dto = nullptr;

  // RAII Guard. Ім'я cRG - Context Restore Guard.
  class CRG {
    View* const v;
    Record* const p;  // previous
  public:
    CRG(View* view) : v(view), p(view->contextRecord) {}
    ~CRG() { v->contextRecord = p; }
  };
  Record* contextRecord = nullptr;
};

// ... (решта коду session_view.h)
// --- Session: "Тупий склад хвостів" ---

class Session {
public:
  // --- Структури для меню ---
  struct Menu {
    roid_t ruid = 0;
    std::string name;
    std::string icon;
    std::string shortcut;
  };
  struct App {
    std::string name;
    std::string icon;
    std::string shortcut;
    std::vector<Menu> menus;
  };

  Session(const std::string& user_login, const std::string& media_type);
  ~Session();

  // Заборона копіювання
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  // --- Керування ланцюжками (Робочими вкладками) ---

  View* createStack(roid_t menu_ruid);
  void listStacks();
  bool switchStack(roid_t stack_ruid);

  void setActive(View*);

private:
  // View отримує прямий доступ для маніпуляції станом сесії ("вишивання")
  friend class View;

  void buildMenu();
  const Layout* findBestLayout(const QModel* qmodel, const string& usage) {
    return rack.findBestLayout("", qmodel, media, usage);
  }

  const Rack& rack;
  string login;
  string media;

  std::vector<App> apps;
  std::map<roid_t, const Layout*> menu_layout_map;

  roid_t active_stack;
  std::map<roid_t, View*> stacks;
  // Вказівник на активний View активного ланцюжка
  View* active_view = nullptr;
};

}  // namespace ky
