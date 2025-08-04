#include <assert.h>

#include <stdexcept>

#include "rack.h"

namespace ky {

// --- Допоміжні функції та визначення ---
// Допоміжна функція для парсингу рядка з прапорами через кому
flags_t parse_flags_from_string(sv str) {
  flags_t flags;
  size_t start = 0;
  while (start < str.length()) {
    size_t end = str.find(',', start);
    if (end == sv::npos) {
      end = str.length();
    }
    sv token = str.substr(start, end - start);
    if (!token.empty()) {
      flags.insert(string{token});
    }
    start = end + 1;
  }
  return flags;
}
// Попереднє оголошення рекурсивної функції
void transform_node(Rack& rack, std::unique_ptr<LayoutNode>& node_ptr);  // Forward declaration

// --- Реалізація Rack::finalize ---

void Rack::finalize_apps() {
  // Етап 2: Фіналізація макетів
  for (auto const& [app_name, app_ptr] : apps.get_map()) {
    if (!std::holds_alternative<App::layvec_t>(app_ptr->layouts)) {
      continue;  // Вже фіналізовано або порожньо
    }

    auto& layout_vec = std::get<App::layvec_t>(app_ptr->layouts);
    App::layset_t final_layout_set;
    final_layout_set.reserve(layout_vec.size());

    for (auto& layout : layout_vec) {
      assert(layout.root_node != nullptr && "цого не може бути - про це парсер мав потурбується");  /// 

      layout.pri = std::stoi(get_default(layout.attrs, string("pri"), string("0")));
      layout.media = parse_flags_from_string(get_default(layout.attrs, string("media"), string("")));
      layout.usage = parse_flags_from_string(get_default(layout.attrs, string("usage"), string("")));

      // 2.2 Рекурсивна трансформація дерева вузлів
      transform_node(*this, layout.root_node);
      final_layout_set.insert(std::move(layout));
    }
    // 2.3 Заміна вектора на unordered_set
    app_ptr->layouts = std::move(final_layout_set);
  }
}
// Рекурсивна функція для трансформації вузлів
void transform_node(Rack& rack, std::unique_ptr<LayoutNode>& node_ptr) {
  assert (node_ptr != nullptr && "як таке може трапитись? загубилося?"); /// 

  // Спочатку обробляємо дочірні вузли (post-order traversal)
  for (auto& child : node_ptr->nodes) {
    transform_node(rack, child);
  }

  std::unique_ptr<LayoutNode> new_node_ptr;
  const string& tag = node_ptr->tag;

  if (tag == "list") {
    auto list_node = std::make_unique<LayoutNodeList>();
    for (const auto& child_node : node_ptr->nodes) {
      auto field = std::make_unique<LayoutField>();
      field->name = child_node->tag;
      field->attrs = child_node->attrs;
      field->flags = child_node->flags;
      list_node->fields.push_back(std::move(field));
    }
    new_node_ptr = std::move(list_node);
  } else if (tag == "fieldbox") {
    auto fieldbox_node = std::make_unique<LayoutNodeFieldBox>();
    for (const auto& child_node : node_ptr->nodes) {
      auto field = std::make_unique<LayoutField>();
      field->name = child_node->tag;
      field->attrs = child_node->attrs;
      field->flags = child_node->flags;
      fieldbox_node->fields.push_back(std::move(field));
    }
    new_node_ptr = std::move(fieldbox_node);
  } else if (tag == "form")
    new_node_ptr = std::make_unique<LayoutNodeForm>();
  else if (tag == "hbox" || tag == "vbox" || tag == "tabbox")
    new_node_ptr = std::make_unique<LayoutNodeBox>();
  else
    return;  // Не є контейнером, що спеціалізується

  new_node_ptr->tag = std::move(node_ptr->tag);
  new_node_ptr->attrs = std::move(node_ptr->attrs);
  new_node_ptr->flags = std::move(node_ptr->flags);
  // Дочірні вузли переміщуються, якщо це не list/fieldbox (де вони були
  // "спожиті")
  if (tag != "list" && tag != "fieldbox") {
    new_node_ptr->nodes = std::move(node_ptr->nodes);
  }
  node_ptr = std::move(new_node_ptr);
}

void Rack::finalize_id() {
  type_t* id_type = types.get("id");
  for (const auto& [_, table_ptr] : tables.get_map())
    table_ptr->fields.get("id")->type = id_type;
}

void Rack::finalize() {
  finalize_id();
  // Викликаємо фіналізацію типів, щоб розв'язати посилання
  type_t::finalize(*this);
  // Викликаємо фіналізацію додатків, щоб трансформувати макети
  finalize_apps();
}


}  // namespace ky