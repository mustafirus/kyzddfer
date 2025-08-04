#include "rack.h"

#include <charconv>
#include <variant>

namespace ky {

// svparts_t
svparts_t::svparts_t(sv name) {
  for (auto pos = name.find('.'); sv::npos != pos; pos = name.find('.')) {
    parts.push(name.substr(0, pos));
    name.remove_prefix(pos + 1);
  }
  parts.push(name);
}

sv svparts_t::pop() {
  assert(!parts.empty() && "Attempted to pop from an empty svparts_t queue");
  auto s = parts.front();
  parts.pop();
  return s;
}

// LayoutNode
LayoutNode::LayoutNode(const LayoutNode& other) : tag(other.tag), attrs(other.attrs), flags(other.flags) {
  nodes.reserve(other.nodes.size());
  for (const auto& node : other.nodes) {
    nodes.push_back(node->clone());
  }
}

std::unique_ptr<LayoutNode> LayoutNode::clone() const { return std::make_unique<LayoutNode>(*this); }

// LayoutNodeList
LayoutNodeList::LayoutNodeList(const LayoutNodeList& other) : LayoutNode(other) {
  fields.reserve(other.fields.size());
  for (const auto& field : other.fields) {
    fields.push_back(field->clone());
  }
}

// LayoutNodeFieldBox
LayoutNodeFieldBox::LayoutNodeFieldBox(const LayoutNodeFieldBox& other) : LayoutNode(other) {
  fields.reserve(other.fields.size());
  for (const auto& field : other.fields) {
    fields.push_back(field->clone());
  }
}

// QTable
QField* QTable::getQField(svparts_t& parts) const {
  assert(parts && "svparts_t cannot be empty here");
  // NOTE: автоматичне додання поля "display_name" не працює
  // NOTE: sv name = parts.pop().value_or(sv{"display_name"});
  sv name = parts.pop();
  const Field* pf = pt->fields[name];
  if (parts) {
    assert(pf->type->is_ref() && "MUST be ref");
    // Створюємо дочірній QTable, передаючи йому поле-ключ 'pf'
    const Table* pt_ref = pf->type->ref();
    QTable* pqt = get_or_create(qtables, name, pt_ref, this, pf);
    return pqt->getQField(parts);
  }
  return get_or_create(qfields, name, this, pf);
}

string QTable::next_alias() {
  static std::atomic<int> num = {1};
  return string{"t"} + std::to_string(num.fetch_add(1));
}

// MTable
QModel::QModel(sv name) : QTable(Rack::get().tables[name]), name(QTable::pt->name) {}

const QField* QModel::getQField(sv fullname) const {
  auto [it, inserted] = qfields.try_emplace(string(fullname), nullptr);
  if (inserted) {
    svparts_t parts{fullname};
    it->second = QTable::getQField(parts);
  }
  return it->second;
}

// Rack
const Rack& Rack::get() {
  static Rack r;
  return r;
}

string Rack::generate_sql() const {
  // Ця функція призначена для генерації SQL-схеми, але наразі не реалізована.
  return "-- SQL schema generation is not implemented yet.\n";
}

/*
void Rack::refs_to_ptr() {
    for (auto const& [type_name, type_ptr] : types.get_map()) {
        if (auto* ref_type = dynamic_cast<ref_t*>(type_ptr)) {
            if (auto* name_ptr = std::get_if<std::string>(&ref_type->table_ref)) {
                if (name_ptr->empty()) continue; // Пропускаємо порожні посилання

                try {
                    // Використовуємо get(), щоб уникнути створення нових елементів у разі помилки
                    ref_type->table_ref = tables.get(*name_ptr);
                } catch (const std::out_of_range&) {
                    std::cerr << "Warning: type '" << type_name << "' references non-existent table '"
                              << *name_ptr << "'. The reference will remain unresolved." << std::endl;
                    // Посилання залишається нерозв'язаним (у вигляді рядка)
                }
            }
        }
    }
}

void Rack::refs_to_string() {
    for (auto const& [type_name, type_ptr] : types.get_map()) {
        if (auto* ref_type = dynamic_cast<ref_t*>(type_ptr)) {
            if (auto* tbl_ptr_ptr = std::get_if<const Table*>(&ref_type->table_ref)) {
                if (*tbl_ptr_ptr) {
                    ref_type->table_ref = (*tbl_ptr_ptr)->name;
                } else {
                    // Обробка нульового вказівника, якщо це можливо
                    ref_type->table_ref = std::string{};
                }
            }
        }
    }
}
*/

namespace {  // anonymous namespace
void count_nodes_recursive(const LayoutNode* node, size_t& count) {
  if (!node) {
    return;
  }
  count++;  // Рахуємо поточний вузол
  for (const auto& child : node->nodes) {
    count_nodes_recursive(child.get(), count);
  }
}
}  // namespace

void Rack::print_stats() const {
  size_t table_count = tables.get_map().size();
  size_t field_count = 0;
  for (const auto& [name, table_ptr] : tables.get_map()) {
    field_count += table_ptr->fields.get_map().size();
  }

  size_t app_count = apps.get_map().size();
  size_t layout_count = 0;
  size_t layout_node_count = 0;

  for (const auto& [name, app_ptr] : apps.get_map()) {
    std::visit(
        [&](auto&& arg) {
          using T = std::decay_t<decltype(arg)>;
          // Обробка обох варіантів: вектора (до фіналізації) та множини (після)
          if constexpr (std::is_same_v<T, App::layvec_t> || std::is_same_v<T, App::layset_t>) {
            layout_count += arg.size();
            for (const auto& layout : arg) {
              count_nodes_recursive(layout.root_node.get(), layout_node_count);
            }
          }
        },
        app_ptr->layouts);
  }

  std::cout << "\n--- Rack Statistics ---\n";
  std::cout << "Tables:        " << table_count << "\n";
  std::cout << "Fields:        " << field_count << "\n";
  std::cout << "Apps:          " << app_count << "\n";
  std::cout << "Layouts:       " << layout_count << "\n";
  std::cout << "Layout Nodes:  " << layout_node_count << "\n";
  std::cout << "-----------------------\n";
}

}  // namespace ky