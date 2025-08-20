#include "session_view.h"
//#include <map>

namespace ky {

// --- Builder: Прихована деталь реалізації ---
class View::Builder {
public:
  // Конструктор отримує все необхідне для роботи
  Builder(View* view, const LayoutNode* node, const RKey* rkey, const node2dto_t& node2dto)
      : view(view), rkey(rkey), node2dto(node2dto) {
    create_records_recursive(node);
  }

private:
  // CRG та contextRecord тепер є приватною справою Builder-а
  class CRG {
    Builder* const b;
    Record* const p;

  public:
    CRG(Builder* builder) : b(builder), p(builder->contextRecord) {}
    ~CRG() { b->contextRecord = p; }
  };

  void View::Builder::create_records_recursive(const LayoutNode* node) {
    CRG guard(this);
    assert(node != nullptr && "LayoutNode не може бути nullptr в рекурсивному обході");

    Record* new_rec = nullptr;
    if (auto form_node = dynamic_cast<const LayoutNodeForm*>(node)) {
      // : Логіка створення ky::Record details or master-details
      const RKey* rk = contextRecord ? &contextRecord->rkey : rkey;
      assert(rk && "Form must have a Recordset context");
      new_rec = new Record(*rk);
    } else if (auto list_node = dynamic_cast<const LayoutNodeList*>(node)) {
      // TODO: Логіка створення ky::Recordset top, lookup, childlist
      if (contextRecord) {
        //->childlist
        const string& table = list_node->attrs.at("table");
        const QModel& qmodel = *rack.qmodels[table];
        sv link = get_default(list_node->attrs, "link", table);
        assert(link.find(':') == std::string_view::npos && "Link with non-id field UNIMPLEMENTED");
        new_rec = new Recordset(qmodel, contextRecord->rkey, link);
      } else if (rkey) {
        //->lookup
        new_rec = new Recordset(*(const_cast<RField*>(rkey->srcRField)));
      } else {
        //->top
        const string& table = list_node->attrs.at("table");
        const QModel& qmodel = *rack.qmodels[table];
        new_rec = new Recordset(qmodel);
      }
    }
    if(new_rec !=nullptr){
      view->records.emplace(rack.ruid32(), contextRecord = new_rec);
      contextRecord->dto = node2dto.at(node);
    }
    

    for (const auto& child_node : node->nodes) {
      create_records_recursive(child_node.get());
    }
  }

  void create_records_recursive(const LayoutNode* node);

  View* view = nullptr;  // Вказівник на View, який ми "будуємо"
  const RKey* rkey = nullptr;
  Record* contextRecord = nullptr;
  const node2dto_t& node2dto;
  const Rack& rack = Rack::get();
};

// --- Реалізація методів View ---
// View::View(Session& session, const Layout& layout, View* prev)

View::View(Session& session, View* prev, const Layout& layout, const RKey* rkey) : session(session), prev(prev) {
  // Етап 1: Викликаємо статичний метод для створення DTO
  node2dto_t node2dto;
  dto = View::makeDto(layout, node2dto);

  // Етап 2: Створення логічних об'єктів
  assert(layout.root_node);
  Builder(this, layout.root_node.get(), rkey, node2dto);

  // if (layout.root_node) {
  //   create_records_recursive(layout.root_node.get(), node_to_dto_map);
  // }

  session.setActive(this);
}

View::~View() {
  session.setActive(prev);
  // Викликаємо статичний метод для коректного звільнення пам'яті DTO
  if (this->dto) {
    View::killDto(this->dto);
  }
}

void View::close() { delete this; }

// --- Реалізація методів Session ---

// ... (решта вашого коду для Session)
Session::Session(const std::string& user_login, const std::string& media_type)
    : rack(Rack::get()), login(user_login), media(media_type) {
  buildMenu();
}

Session::~Session() {
  while (active_view) {
    delete active_view;
  };
}

void Session::setActive(View* view) {
  if (view != nullptr) {
    stacks[active_stack] = view;
    active_view = view;
  } else {
    stacks.erase(active_stack);
    auto it = stacks.begin();
    if (it != stacks.end()) {
      active_stack = it->first;
      active_view = it->second;
    } else {
      active_stack = 0;
      active_view = nullptr;
    }
  }
}
void Session::buildMenu() {
  const auto& apps_map = rack.apps.get_map();
  for (const auto& [app_name, app_ptr] : apps_map) {
    App current_app_dto;
    current_app_dto.name = get_default(app_ptr->attrs, std::string("title"), std::string(app_name));
    current_app_dto.icon = get_default(app_ptr->attrs, std::string("icon"), std::string(""));
    current_app_dto.shortcut = get_default(app_ptr->attrs, std::string("shortcut"), std::string(""));

    assert(std::holds_alternative<ky::App::layset_t>(app_ptr->layouts));
    const auto& layout_set = std::get<ky::App::layset_t>(app_ptr->layouts);

    for (const Layout& layout : layout_set) {
      if (layout.usage.count("menu") && (layout.media.empty() || layout.media.count(media))) {
        // const roid_t new_menu_ruid = ruid_gen(); // ruid_gen() is not defined, placeholder
        const roid_t new_menu_ruid = 0;
        menu_layout_map[new_menu_ruid] = &layout;

        Menu menu_item_dto;
        menu_item_dto.ruid = new_menu_ruid;
        menu_item_dto.name = get_default(layout.attrs, std::string("title"), layout.name);
        menu_item_dto.icon = get_default(layout.attrs, std::string("icon"), std::string(""));
        menu_item_dto.shortcut = get_default(layout.attrs, std::string("shortcut"), std::string(""));
        current_app_dto.menus.push_back(std::move(menu_item_dto));
      }
    }
    if (!current_app_dto.menus.empty()) {
      apps.push_back(std::move(current_app_dto));
    }
  }
}
}  // namespace ky
