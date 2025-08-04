#include <iostream>
#include <string>
#include <unordered_map>
#include <list>
#include <set>
#include <optional>
#include <algorithm>
#include <vector>
#include <mutex>
#include <thread>
#include <memory>

template <typename T>
class AdaptiveReplacementCache {
private:
    struct CacheEntry {
        T value;
        int useCount;
        bool is_in_lfu;

        typename std::list<std::string>::iterator lru_it;
        typename std::set<std::pair<int, std::string>>::iterator lfu_it;
    };

    mutable std::mutex cache_mutex;
    std::unordered_map<std::string, CacheEntry> cache;
    std::list<std::string> lru_keys;
    std::set<std::pair<int, std::string>> lfu_keys;

    // --- Конфігурація ---
    int softLimit;
    int hardLimit;
    int lfuThreshold;
    const int minLimit;

    // --- Статистика ---
    int total_accesses{0};
    int hits{0};
    
    // --- Приватні методи (викликаються під блокуванням) ---

    // Повністю видаляє запис з усіх структур
    void remove_entry(typename std::unordered_map<std::string, CacheEntry>::iterator it) {
        CacheEntry& entry = it->second;
        if (entry.is_in_lfu) {
            lfu_keys.erase(entry.lfu_it);
        } else {
            lru_keys.erase(entry.lru_it);
        }
        cache.erase(it);
    }

    void evict() {
        if (lfu_keys.size() > lru_keys.size() && !lfu_keys.empty()) {
            evict_from_lfu();
        } else {
            evict_from_lru();
        }
    }

    void evict_from_lru() {
        if (lru_keys.empty()) return;
        const std::string& key_to_evict = lru_keys.back();
        // Спочатку знаходимо ітератор в мапі, щоб потім передати його в remove_entry
        auto it = cache.find(key_to_evict);
        if (it != cache.end()) {
            remove_entry(it);
        }
    }

    void evict_from_lfu() {
        if (lfu_keys.empty()) return;
        auto it_lfu = lfu_keys.begin();
        const std::string& key_to_evict = it_lfu->second;
        // Спочатку знаходимо ітератор в мапі, щоб потім передати його в remove_entry
        auto it_cache = cache.find(key_to_evict);
        if (it_cache != cache.end()) {
            remove_entry(it_cache);
        }
    }

    void move_to_lfu(typename std::unordered_map<std::string, CacheEntry>::iterator cache_it) {
        const std::string& key = cache_it->first;
        CacheEntry& entry = cache_it->second;
        lru_keys.erase(entry.lru_it);
        entry.lfu_it = lfu_keys.insert({entry.useCount, key}).first;
        entry.is_in_lfu = true;
    }

    void trim() {
        while (cache.size() > softLimit) {
            evict();
        }
    }

    void adjust_size() {
        double hit_rate = static_cast<double>(hits) / total_accesses;
        bool decreased = false;
        if (hit_rate < 0.50 && softLimit < hardLimit) {
            int old_limit = softLimit;
            softLimit += std::max(1, hardLimit / 10);
            softLimit = std::min(softLimit, hardLimit);
        } else if (hit_rate > 0.90 && softLimit > minLimit) {
            int old_limit = softLimit;
            softLimit -= std::max(1, hardLimit / 10);
            softLimit = std::max(softLimit, minLimit);
            if (softLimit < old_limit) {
                 decreased = true;
            }
        }
        if (decreased && cache.size() > softLimit) {
            trim();
        }
        hits = 0;
        total_accesses = 0;
    }

public:
    explicit AdaptiveReplacementCache(int hard_limit)
        : hardLimit(hard_limit), 
          minLimit(std::max(1, hard_limit / 10))
    {
        softLimit = minLimit;
        lfuThreshold = std::max(2, hard_limit / 20);
    }

    // Змінено для прийняття r-value reference і повернення T*
    T* put(const std::string& key, T&& value) {
        std::lock_guard<std::mutex> lock(cache_mutex);

        auto it = cache.find(key);
        if (it != cache.end()) {
            // Повністю видаляємо старий запис перед вставкою нового.
            // Це гарантує коректне знищення RAII об'єкта і скидання метаданих.
            remove_entry(it);
        }

        if (cache.size() >= softLimit) {
            evict();
        }
        
        CacheEntry new_entry;
        new_entry.value = std::move(value); // Переміщуємо, а не копіюємо
        new_entry.useCount = 1;
        new_entry.is_in_lfu = false;
        
        lru_keys.push_front(key);
        new_entry.lru_it = lru_keys.begin();
        
        // emplace повертає пару, перший елемент якої - ітератор на вставлений елемент
        auto emplace_result = cache.emplace(key, std::move(new_entry));
        // Повертаємо вказівник на щойно вставлене значення
        return &emplace_result.first->second.value;
    }

    // Змінено для повернення вказівника
    T* get(const std::string& key) {
        std::lock_guard<std::mutex> lock(cache_mutex);

        total_accesses++;
        int adjustment_interval = softLimit * 10;
        if (total_accesses >= adjustment_interval && adjustment_interval > 0) {
            adjust_size();
        }
        auto it = cache.find(key);
        if (it == cache.end()) {
            return nullptr; // Повертаємо nullptr у разі промаху
        }
        hits++;
        CacheEntry& entry = it->second;
        entry.useCount++;
        if (entry.is_in_lfu) {
            lfu_keys.erase(entry.lfu_it);
            entry.lfu_it = lfu_keys.insert({entry.useCount, key}).first;
        } else {
            lru_keys.erase(entry.lru_it);
            lru_keys.push_front(key);
            entry.lru_it = lru_keys.begin();
            if (entry.useCount > lfuThreshold) {
                move_to_lfu(it);
            }
        }
        return &entry.value; // Повертаємо вказівник на значення
    }
};

