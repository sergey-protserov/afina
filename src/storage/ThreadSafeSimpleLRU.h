#ifndef AFINA_STORAGE_THREAD_SAFE_SIMPLE_LRU_H
#define AFINA_STORAGE_THREAD_SAFE_SIMPLE_LRU_H

/* TOASK: не понял смысл замечания из Wiki:
внимание: т.к все вызываемые методы интерфейса storage виртуальный, то "делегирование"
может быть сделано через наследование, как написано в коде afina, однако вы можете и
переписать этот код, сохранив ссылку на SimpleLRU явно
*/

#include <map>
#include <mutex>
#include <string>

#include "SimpleLRU.h"

namespace Afina {
namespace Backend {

/**
 * # SimpleLRU thread safe version
 *
 *
 */
// SINCE ACCORDING TO LRU LOGIC, ELEMENT'S POSITION IN CACHE
// MUST BE UPDATED ON EACH READ, THERE ARE NO THREAD-SAFE
// OPERATIONS AT ALL.
// AS SUCH, I WILL USE SINGLE GLOBAL MUTEX TO PROTECT
// EACH AND EVERY OPERATION.
class ThreadSafeSimplLRU : public SimpleLRU {
public:
    ThreadSafeSimplLRU(size_t max_size = 1024) : SimpleLRU(max_size) {}
    ~ThreadSafeSimplLRU() {}

    // see SimpleLRU.h
    bool Put(const std::string &key, const std::string &value) override {
        std::lock_guard<std::mutex> lg(_m);
        return SimpleLRU::Put(key, value);
    }

    // see SimpleLRU.h
    bool PutIfAbsent(const std::string &key, const std::string &value) override {
        std::lock_guard<std::mutex> lg(_m);
        return SimpleLRU::PutIfAbsent(key, value);
    }

    // see SimpleLRU.h
    bool Set(const std::string &key, const std::string &value) override {
        std::lock_guard<std::mutex> lg(_m);
        return SimpleLRU::Set(key, value);
    }

    // see SimpleLRU.h
    bool Delete(const std::string &key) override {
        std::lock_guard<std::mutex> lg(_m);
        return SimpleLRU::Delete(key);
    }

    // see SimpleLRU.h
    // Get is no longer const, since according to LRU logic, it should update element's position
    bool Get(const std::string &key, std::string &value) override {
        std::lock_guard<std::mutex> lg(_m);
        return SimpleLRU::Get(key, value);
    }

private:
    std::mutex _m;
};

} // namespace Backend
} // namespace Afina

#endif // AFINA_STORAGE_THREAD_SAFE_SIMPLE_LRU_H
