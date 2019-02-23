#include "SimpleLRU.h"

namespace Afina {
namespace Backend {

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Put(const std::string &key, const std::string &value) {
    auto found_it = _lru_index.find(key);
    if (found_it == _lru_index.end()) {
        return PutImpl(key, value);
    }
    return SetImpl(found_it, value);
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::PutIfAbsent(const std::string &key, const std::string &value) {
    if (_lru_index.find(key) != _lru_index.end()) {
        return false;
    }
    return PutImpl(key, value);
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Set(const std::string &key, const std::string &value) {
    auto found_it = _lru_index.find(key);
    if (found_it == _lru_index.end()) {
        return false;
    }
    return SetImpl(found_it, value);
}

// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Delete(const std::string &key) {
    auto todel_it = _lru_index.find(key);
    if (todel_it == _lru_index.end()) {
        return false;
    }

    return DeleteItImpl(todel_it);
}

// TOASK: если виртуальная функция - не const, то может ли её
// override быть const?
// See MapBasedGlobalLockImpl.h
bool SimpleLRU::Get(const std::string &key, std::string &value) {
    auto found_it = _lru_index.find(key);
    if (found_it == _lru_index.end()) {
        return false;
    }

    value = found_it->second.get().value;
    return RefreshImp(found_it->second.get());
}

// Delete node by it's iterator in _lru_index
bool SimpleLRU::DeleteItImpl(std::map<std::reference_wrapper<const std::string>, std::reference_wrapper<lru_node>,
                                      std::less<std::string>>::iterator todel_it) {
    std::unique_ptr<lru_node> tmp;
    lru_node &todel_node = todel_it->second;
    _cur_size -= todel_node.key.size() + todel_node.value.size();
    if (todel_node.next) {
        todel_node.next->prev = todel_node.prev;
    }
    if (todel_node.prev) {
        tmp.swap(todel_node.prev->next); // extend lifetime of todel_node
        todel_node.prev->next = std::move(todel_node.next);
    } else {
        tmp.swap(_lru_head); // extend lifetime of todel_node
        _lru_head = std::move(todel_node.next);
    }
    // TOASK: всё-таки не понял, при каких обстоятельствах может понадобиться
    // todel_node. Зачем мы продлеваем его lifetime?
    _lru_index.erase(todel_it);
    return true;
}

// Delete node by it's reference
bool SimpleLRU::DeleteRefImpl(lru_node &todel_ref) {
    std::unique_ptr<lru_node> tmp;
    _cur_size -= todel_ref.key.size() + todel_ref.value.size();
    if (todel_ref.next) {
        todel_ref.next->prev = todel_ref.prev;
    }
    if (todel_ref.prev) {
        tmp.swap(todel_ref.prev->next); // extend lifetime of todel_ref
        todel_ref.prev->next = std::move(todel_ref.next);
    } else {
        tmp.swap(_lru_head); // extend lifetime of todel_ref
        _lru_head = std::move(todel_ref.next);
    }
    _lru_index.erase(todel_ref.key);
    return true;
}

// Refresh node by it's reference
// We don't need an iterator here, since _lru_index is a map of references
// and if we carefully handle all the pointers, _lru_index needs not to be changed
bool SimpleLRU::RefreshImp(lru_node &torefresh_ref) {
    if (&torefresh_ref == _lru_tail) {
        return true;
    }
    if (&torefresh_ref == _lru_head.get()) {
        _lru_head.swap(torefresh_ref.next);
        _lru_head->prev = nullptr;
    } else {
        torefresh_ref.next->prev = torefresh_ref.prev;
        torefresh_ref.prev->next.swap(torefresh_ref.next);
    }
    _lru_tail->next.swap(torefresh_ref.next);
    torefresh_ref.prev = _lru_tail;
    _lru_tail = &torefresh_ref;
    return true;
}

// Remove LRU-nodes until we get as much as needfree free space
bool SimpleLRU::GetFreeImpl(size_t needfree) {
    if (needfree > _max_size) {
        return false;
    }
    while (_max_size - _cur_size < needfree) {
        DeleteRefImpl(*_lru_head);
    }
    return true;
}

// Put a new element w/o checking for it's existence (this check MUST be performed before calling this method)
bool SimpleLRU::PutImpl(const std::string &key, const std::string &value) {
    size_t addsize = key.size() + value.size();
    if (!GetFreeImpl(addsize)) {
        return false;
    }
    // TOASK: что будет с остальными полями структуры, которые я не указываю в списке инициализации?
    // см. вопрос в SimpleLRU.h: там в указателе был мусор, если не инициализировать его явно
    std::unique_ptr<lru_node> toput{new lru_node{key, value}};
    if (_lru_tail != nullptr) {
        toput->prev = _lru_tail;
        _lru_tail->next.swap(toput);
        _lru_tail = _lru_tail->next.get();
    } else {
        _lru_head.swap(toput);
        _lru_tail = _lru_head.get();
    }
    _lru_index.insert(std::make_pair(std::reference_wrapper<const std::string>(_lru_tail->key),
                                     std::reference_wrapper<lru_node>(*_lru_tail)));
    _cur_size += addsize;
    return true;
}

// Set element value by _lru_index iterator
bool SimpleLRU::SetImpl(std::map<std::reference_wrapper<const std::string>, std::reference_wrapper<lru_node>,
                                 std::less<std::string>>::iterator toset_it,
                        const std::string &value) {
    lru_node &toset_node = toset_it->second;
    size_t sizedelta = value.size() - toset_node.value.size();
    // if (_cur_size + sizedelta > _max_size) {
    //     return false;
    // }
    if (!GetFreeImpl(sizedelta)) {
        return false;
    }
    toset_node.value = value;
    _cur_size += sizedelta;
    return RefreshImp(toset_node);
}

} // namespace Backend
} // namespace Afina
