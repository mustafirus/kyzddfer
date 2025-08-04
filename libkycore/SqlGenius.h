#pragma once

#include "rec.h"
#include "rack.h" 
#include <algorithm>
#include <cassert>
#include <cctype>
#include <functional>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <iostream>


namespace ky {

// Попередні оголошення
class Record;
class Recordset;
class RField;

// Псевдоніми для типів
using qfields_t = std::vector<const QField*>;
using qtusedmap_t = std::map<sv, const QTable*>;

/**
 * @brief Допоміжний клас для розбору одного рядка фільтра.
 * @details Інкапсулює логіку перетворення рядка типу ">100|20:30" 
 * в готовий до використання SQL-блок та набір параметрів.
 */
class FilterParser {
public:
    /**
     * @brief Конструктор, який одразу виконує парсинг.
     * @param filter Об'єкт фільтра з Recordset, що містить поле та рядок значень.
     */
    explicit FilterParser(const Recordset::Filter& filter) 
        : input_filter_(filter) {
        parse();
    }

    /// Повертає згенерований SQL-блок, напр. "(t1.status LIKE $f_0 OR t1.amount > $f_1)"
    const std::string& getSql() const { return sql_clause_; }

    /// Повертає параметри, що належать тільки цьому блоку.
    const std::map<std::string, std::string>& getParams() const { return params_; }

private:
    void parse() {
        std::stringstream ss;
        const auto& full_field_name = input_filter_.rfield.qfield.pqt->alias 
                                      + "." + input_filter_.rfield.qfield.pf->sqlName();

        std::string value_str(input_filter_.value);
        std::stringstream value_stream(value_str);
        std::string segment;
        bool first_or = true;

        ss << "(";

        while (std::getline(value_stream, segment, '|')) {
            if (segment.empty()) continue;

            if (!first_or) {
                ss << " OR ";
            }
            first_or = false;
            
            std::string param_name = "f_" + std::to_string(local_param_counter_++);
            
            // Визначення оператора
            if (segment.rfind("!=", 0) == 0) {
                ss << full_field_name << " != $" << param_name;
                params_[param_name] = segment.substr(2);
            } else if (segment[0] == '>') {
                ss << full_field_name << " > $" << param_name;
                params_[param_name] = segment.substr(1);
            } else if (segment[0] == '<') {
                ss << full_field_name << " < $" << param_name;
                params_[param_name] = segment.substr(1);
            } else {
                auto pos = segment.find(':');
                if (pos != std::string::npos) {
                    std::string param_name2 = "f_" + std::to_string(local_param_counter_++);
                    ss << full_field_name << " BETWEEN $" << param_name << " AND $" << param_name2;
                    params_[param_name] = segment.substr(0, pos);
                    params_[param_name2] = segment.substr(pos + 1);
                } else {
                    ss << full_field_name << " LIKE $" << param_name;
                    params_[param_name] = segment + "%";
                }
            }
        }
        ss << ")";
        sql_clause_ = ss.str();
    }

    const Recordset::Filter& input_filter_;
    std::string sql_clause_;
    std::map<std::string, std::string> params_;
    int local_param_counter_ = 0;
};


/**
 * @brief Клас, що генерує SQL. Є friend-класом для Record та Recordset.
 * Підтримує 3-кроковий процес завантаження для Recordset та динамічний набір полів.
 */
class SqlGenius {
public:
    explicit SqlGenius(Record* rec) : record(rec), recordset(nullptr) {
        if (!record) {
            throw std::invalid_argument("SqlGenius: вказівник на Record не може бути нульовим.");
        }
        recordset = dynamic_cast<Recordset*>(record);
    }

    // --- Методи для Record (прості запити) ---

    /**
     * @brief Генерує запит SELECT для завантаження одного запису (Record).
     * @details Цей метод призначений для сценаріїв, коли потрібно завантажити
     * дані для одного конкретного запису за його ID.
     * Він не використовується
     * для Recordset, оскільки для них застосовується 3-кроковий процес завантаження.
     * @param fields_to_load Вектор полів, які потрібно завантажити.
     * @return Рядок з готовим SQL-запитом.
     */
    std::string gen_select_one(const vector_prf& fields_to_load) {
        if (fields_to_load.empty()) return "";
        namedParams.clear();
        std::stringstream ss;
        
        qfields_t qfields;
        qtusedmap_t used_tables;
        build_clauses_from_fields(fields_to_load, qfields, used_tables);

        sql_clause_select(ss, qfields);
        sql_clause_from(ss, used_tables);
        sql_clause_where_id(ss);
        namedParams["id"] = getIdFieldValue();
        ss << ";";
        return ss.str();
    }

    std::string gen_insert() {
        namedParams.clear();
        const auto& rkey = record->rkey;
        const auto* mtable = rkey.tgtQModel;

        std::stringstream columns, values;
        bool first = true;
        for (const auto& rf_ptr : record->rfields) {
            if (rf_ptr->qfield.pqt != mtable || !rf_ptr->is_modified || rf_ptr->qfield.pf->name == "id") continue;
            if (!first) {
                columns << ", ";
                values << ", ";
            }
            first = false;
            const auto& param_name = rf_ptr->qfield.pf->sqlName();
            columns << param_name;
            values << "$" << param_name;
            namedParams[param_name] = std::string(rf_ptr->val);
        }

        if (first) return ""; // No fields to insert

        std::stringstream ss;
        ss << "INSERT INTO " << mtable->pt->name << " (" << columns.str() << ") VALUES (" << values.str() << ") RETURNING id;";
        return ss.str();
    }

    std::string gen_update() {
        namedParams.clear();
        const auto& rkey = record->rkey;
        const auto* mtable = rkey.tgtQModel;
        std::stringstream set_clause;
        bool first = true;
        for (const auto& rf_ptr : record->rfields) {
            if (rf_ptr->qfield.pqt != mtable || !rf_ptr->is_modified || rf_ptr->qfield.pf->name == "id") continue;
            if (!first) set_clause << ", ";
            first = false;

            const auto& param_name = rf_ptr->qfield.pf->sqlName();
            set_clause << param_name << " = $" << param_name;
            namedParams[param_name] = std::string(rf_ptr->val);
        }

        if (first) return ""; // No fields to update

        std::stringstream ss;
        ss << "UPDATE " << mtable->pt->name << " SET " << set_clause.str();
        sql_clause_where_id(ss);
        namedParams["id"] = getIdFieldValue();

        ss << ";";
        return ss.str();
    }

    std::string gen_delete() {
        namedParams.clear();
        const auto& rkey = record->rkey;
        const auto* mtable = rkey.tgtQModel;
        std::stringstream ss;
        ss << "DELETE FROM " << mtable->pt->name;
        
        sql_clause_where_id(ss);
        namedParams["id"] = getIdFieldValue();
        
        ss << ";";
        return ss.str();
    }

    /**
     * @brief Генерує запит DELETE для списку ID.
     * @param ids Вектор ID записів, які потрібно видалити.
     * @return Рядок з готовим SQL-запитом "DELETE ... WHERE id IN (...)".
     */
    std::string gen_delete_by_ids(const std::vector<std::string>& ids) {
        if (ids.empty()) return "";
        namedParams.clear();

        const auto* mtable = record->rkey.tgtQModel;
        std::stringstream ss;
        ss << "DELETE FROM " << mtable->pt->name;
        
        ss << "\nWHERE id IN (";
        bool first = true;
        for (size_t i = 0; i < ids.size(); ++i) {
            if (!first) ss << ", ";
            first = false;
            
            // Використовуємо префікс 'id_' для уникнення конфліктів
            std::string param_name = "id_" + std::to_string(i);
            ss << "$" << param_name;
            namedParams[param_name] = ids[i];
        }
        ss << ");";
        
        return ss.str();
    }

    // --- Методи для Recordset (3-крокове завантаження) ---

    std::string gen_select_count() {
        if (!recordset) throw std::logic_error("gen_select_count can only be called for a Recordset.");
        namedParams.clear();
        
        qtusedmap_t used_tables;
        const auto* mtable = recordset->rkey.tgtQModel;
        used_tables.try_emplace(mtable->alias, mtable);
        add_tables_from_filters(used_tables);

        std::stringstream ss;
        ss << "SELECT COUNT(" << mtable->alias << ".id)";
        sql_clause_from(ss, used_tables);
        sql_clause_where_filters(ss);
        ss << ";";
        return ss.str();
    }

    std::string gen_select_ids() {
        if (!recordset) throw std::logic_error("gen_select_ids can only be called for a Recordset.");
        namedParams.clear();

        qtusedmap_t used_tables;
        const auto* mtable = recordset->rkey.tgtQModel;
        used_tables.try_emplace(mtable->alias, mtable);
        add_tables_from_filters(used_tables);
        add_tables_from_sorts(used_tables);

        std::stringstream ss;
        ss << "SELECT " << mtable->alias << ".id";
        sql_clause_from(ss, used_tables);
        sql_clause_where_filters(ss);
        sql_clause_sort(ss);
        sql_clause_pager(ss);
        ss << ";";
        return ss.str();
    }

    std::string gen_select_by_ids(const vector_prf& fields_to_load, const std::vector<std::string>& ids) {
        if (ids.empty() || fields_to_load.empty()) return "";
        namedParams.clear();

        qfields_t qfields;
        qtusedmap_t used_tables;
        build_clauses_from_fields(fields_to_load, qfields, used_tables);

        std::stringstream ss;
        sql_clause_select(ss, qfields);
        sql_clause_from(ss, used_tables);

        const auto* mtable = record->rkey.tgtQModel;
        ss << "\nWHERE " << mtable->alias << ".id IN (";
        bool first = true;
        for (size_t i = 0; i < ids.size(); ++i) {
            if (!first) ss << ", ";
            first = false;
            std::string param_name = "id_" + std::to_string(i);
            ss << "$" << param_name;
            namedParams[param_name] = ids[i];
        }
        ss << ")";
        sql_clause_sort(ss);
        ss << ";";
        return ss.str();
    }

    std::vector<std::string> getOrderedParams(const std::string& sql) const {
        std::vector<std::string> ordered_params;
        std::string current_param_name;
        bool in_param = false;

        for (size_t i = 0; i < sql.length(); ++i) {
            char c = sql[i];
            if (c == '$') {
                in_param = true;
                current_param_name.clear();
            } else if (in_param) {
                if (std::isalnum(c) || c == '_') {
                    current_param_name += c;
                } else {
                    auto it = namedParams.find(current_param_name);
                    if (it == namedParams.end()) throw std::runtime_error("Parameter not found: " + current_param_name);
                    ordered_params.push_back(it->second);
                    in_param = false;
                }
            }
        }
        if (in_param && !current_param_name.empty()) {
            auto it = namedParams.find(current_param_name);
            if (it == namedParams.end()) throw std::runtime_error("Parameter not found: " + current_param_name);
            ordered_params.push_back(it->second);
        }
        return ordered_params;
    }

private:
    // --- Допоміжні методи ---
    
    void build_clauses_from_fields(const vector_prf& fields, qfields_t& qfields, qtusedmap_t& used_tables) {
        const auto* mtable = record->rkey.tgtQModel;
        used_tables.try_emplace(mtable->alias, mtable);

        for (const auto& rfield_ptr : fields) {
            const QField* qfield = &rfield_ptr->qfield;
            qfields.push_back(qfield);
            const QTable* pqt = qfield->pqt;
            while (pqt) {
                used_tables.try_emplace(pqt->alias, pqt);
                pqt = pqt->ppqt;
            }
        }
    }
    
    void add_tables_from_filters(qtusedmap_t& used_tables) {
         if (!recordset) return;
         for (const auto& filter : recordset->filters) {
             const QTable* pqt = filter.rfield.qfield.pqt;
             while(pqt) {
                 used_tables.try_emplace(pqt->alias, pqt);
                 pqt = pqt->ppqt;
             }
         }
    }

    void add_tables_from_sorts(qtusedmap_t& used_tables) {
        if (!recordset) return;
        for (const auto& s : recordset->sorts) {
            const QTable* pqt = s.rfield.qfield.pqt;
            while(pqt) {
                used_tables.try_emplace(pqt->alias, pqt);
                pqt = pqt->ppqt;
            }
        }
    }

    std::string getIdFieldValue() const {
        const auto* source_field = record->rkey.srcRField;
        if (source_field && !source_field->is_null) {
            return std::string(source_field->val);
        }
        throw std::runtime_error("Database ID for the record is not available via RKey.srcRField.");
    }

    void sql_clause_select(std::stringstream& ss, const qfields_t& qfields) const {
        ss << "SELECT ";
        bool first_field = true;
        for (const auto qfield : qfields) {
            if (!first_field) ss << ", ";
            ss << qfield->pqt->alias << "." << qfield->pf->sqlName() << " AS "
               << qfield->pqt->alias << "_" << qfield->pf->sqlName();
            first_field = false;
        }
    }

    void sql_clause_from(std::stringstream& ss, qtusedmap_t& used_tables) const {
        const auto& rkey = record->rkey;
        const auto* mtable = rkey.tgtQModel;
        ss << "\nFROM " << mtable->pt->name << " AS " << mtable->alias;
        used_tables.erase(mtable->alias);

        std::function<void(const QTable*)> build_joins;
        build_joins = [&](const QTable* pqt) {
            assert(pqt->ppqt && "Non-master QTable must have a parent");
            if (!pqt->ppqt->isMaster()) build_joins(pqt->ppqt);
            if (used_tables.count(pqt->alias) == 0) return;
            
            ss << "\nLEFT JOIN " << pqt->pt->name << " AS " << pqt->alias << " ON "
               << pqt->ppqt->alias << "."
               << pqt->fk_in_parent->sqlName()
               << " = " << pqt->alias << ".id";
            used_tables.erase(pqt->alias);
        };

        while (!used_tables.empty()) {
            build_joins(used_tables.begin()->second);
        }
    }

    void sql_clause_where_id(std::stringstream& ss) const {
        const auto& rkey = record->rkey;
        const auto* mtable = rkey.tgtQModel;
        ss << "\nWHERE " << mtable->alias << ".id = $id";
    }

    // =================================================================
    // <<< НОВА РЕАЛІЗАЦІЯ >>>
    // =================================================================
    void sql_clause_where_filters(std::stringstream& ss) {
        if (!recordset) return;

        /*
         * =================================================================
         * TODO: Майбутня реалізація зв'язку "Один-до-багатьох" (Master-Detail)
         * =================================================================
         */
        
        if (recordset->filters.empty()) {
            return;
        }

        ss << "\nWHERE ";
        bool first_filter_group = true;
        
        for (const auto& filter : recordset->filters) {
            if (!first_filter_group) {
                ss << " AND ";
            }
            
            // Створюємо парсер для кожного фільтра.
            // Вся логіка розбору інкапсульована в ньому.
            FilterParser parser(filter);
            
            // Додаємо згенерований SQL-блок
            ss << parser.getSql();
            
            // Копіюємо згенеровані парсером параметри до загальної мапи
            const auto& new_params = parser.getParams();
            namedParams.insert(new_params.begin(), new_params.end());

            first_filter_group = false;
        }
    }

    void sql_clause_sort(std::stringstream& ss) const {
        if (!recordset || recordset->sorts.empty()) {
            return;
        }
        ss << "\nORDER BY ";
        bool first = true;
        for (const auto& s : recordset->sorts) {
            if (!first) ss << ", ";
            first = false;
            const auto* qfield = &s.rfield.qfield;
            ss << qfield->pqt->alias << "." << qfield->pf->sqlName();
            if (s.dir == Recordset::Sort::Direction::DESC) ss << " DESC";
        }
    }

    void sql_clause_pager(std::stringstream& ss) {
        if (!recordset) return;
        ss << "\nLIMIT $pg_limit OFFSET $pg_offset";
        const auto& pager = recordset->pager;
        namedParams["pg_limit"] = std::to_string(pager.limit);
        namedParams["pg_offset"] = std::to_string(pager.offset);
    }

    // --- Члени класу ---
    Record* record;
    Recordset* recordset;
    std::map<std::string, std::string> namedParams; 
};

} // namespace ky
