/*
 * Copyright (C) 2012 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef ANDROID_UTILS_LRU_CACHE_H
#define ANDROID_UTILS_LRU_CACHE_H

#include <unordered_map>

#include <UniquePtr.h>

#include "utils/TypeHelpers.h" // hash_t

namespace android {

/**
 * GenerationCache callback used when an item is removed
 */
template<typename EntryKey, typename EntryValue>
class OnEntryRemoved {
public:
    virtual ~OnEntryRemoved() { };
    virtual void operator()(EntryKey& key, EntryValue& value) = 0;
}; // class OnEntryRemoved

template <typename TKey, typename TValue>
class LruCache {
public:
    explicit LruCache(uint32_t maxCapacity);
    ~LruCache();

    enum Capacity {
        kUnlimitedCapacity,
    };

    void setOnEntryRemovedListener(OnEntryRemoved<TKey, TValue>* listener);
    size_t size() const;
    const TValue& get(const TKey& key);
    bool put(const TKey& key, const TValue& value);
    bool remove(const TKey& key);
    bool removeOldest();
    void clear();
    const TValue& peekOldestValue();

private:
    LruCache(const LruCache& that);  // disallow copy constructor

    struct HashWithHashType : public std::unary_function<TKey,hash_t> {
        size_t operator() (TKey value) const { return hash_type(value); };
    };

    struct Entry {
        TKey key;
        TValue value;
        Entry* parent;
        Entry* child;

        Entry(TKey key_, TValue value_) : key(key_), value(value_), parent(NULL), child(NULL) {
        }
        const TKey& getKey() const { return key; }
    };

    typedef std::unordered_map<TKey, Entry*, HashWithHashType> LruCacheHashTable;

    void attachToCache(Entry& entry);
    void detachFromCache(Entry& entry);
    void rehash(size_t newCapacity);

    UniquePtr<LruCacheHashTable> mTable;
    OnEntryRemoved<TKey, TValue>* mListener;
    Entry* mOldest;
    Entry* mYoungest;
    uint32_t mMaxCapacity;
    TValue mNullValue;

public:
    class Iterator {
    public:
        Iterator(const LruCache<TKey, TValue>& cache): mCache(cache), mIterator(mCache.mTable->begin()) {
        }

        bool next() {
            if (mIterator == mCache.mTable->end()) {
		return false;
            }
            std::advance(mIterator, 1);
            return mIterator != mCache.mTable->end();
        }

        const TValue& value() const {
            return mIterator->second->value;
        }

        const TKey& key() const {
            return mIterator->second->key;
        }
    private:
        const LruCache<TKey, TValue>& mCache;
        typename LruCacheHashTable::iterator mIterator;
    };
};

// Implementation is here, because it's fully templated
template <typename TKey, typename TValue>
LruCache<TKey, TValue>::LruCache(uint32_t maxCapacity)
    : mTable(new LruCacheHashTable())
    , mListener(NULL)
    , mOldest(NULL)
    , mYoungest(NULL)
    , mMaxCapacity(maxCapacity)
    , mNullValue(NULL) {
    mTable->max_load_factor(1.0);
};

template <typename TKey, typename TValue>
LruCache<TKey, TValue>::~LruCache() {
    // Need to delete created entries.
    clear();
};

template<typename K, typename V>
void LruCache<K, V>::setOnEntryRemovedListener(OnEntryRemoved<K, V>* listener) {
    mListener = listener;
}

template <typename TKey, typename TValue>
size_t LruCache<TKey, TValue>::size() const {
    return mTable->size();
}

template <typename TKey, typename TValue>
const TValue& LruCache<TKey, TValue>::get(const TKey& key) {
    auto find_result = mTable->find(key);
    if (find_result == mTable->end()) {
        return mNullValue;
    }
    Entry *entry = find_result->second;
    detachFromCache(*entry);
    attachToCache(*entry);
    return entry->value;
}

template <typename TKey, typename TValue>
bool LruCache<TKey, TValue>::put(const TKey& key, const TValue& value) {
    if (mMaxCapacity != kUnlimitedCapacity && size() >= mMaxCapacity) {
        removeOldest();
    }

    if (mTable->find(key) != mTable->end()) {
        return false;
    }

    Entry* newEntry = new Entry(key, value);
    mTable->insert(std::make_pair(key, newEntry));
    attachToCache(*newEntry);
    return true;
}

template <typename TKey, typename TValue>
bool LruCache<TKey, TValue>::remove(const TKey& key) {
    auto find_result = mTable->find(key);
    if (find_result == mTable->end()) {
        return false;
    }
    Entry *entry = find_result->second;
    if (mListener) {
        (*mListener)(entry->key, entry->value);
    }
    detachFromCache(*entry);
    mTable->erase(entry->key);
    delete entry;
    return true;
}

template <typename TKey, typename TValue>
bool LruCache<TKey, TValue>::removeOldest() {
    if (mOldest != NULL) {
        return remove(mOldest->key);
        // TODO: should probably abort if false
    }
    return false;
}

template <typename TKey, typename TValue>
const TValue& LruCache<TKey, TValue>::peekOldestValue() {
    if (mOldest) {
        return mOldest->value;
    }
    return mNullValue;
}

template <typename TKey, typename TValue>
void LruCache<TKey, TValue>::clear() {
    if (mListener) {
        for (Entry* p = mOldest; p != NULL; p = p->child) {
            (*mListener)(p->key, p->value);
        }
    }
    mYoungest = NULL;
    mOldest = NULL;
    for (auto key_val : *mTable.get()) {
        delete key_val.second;
    }
    mTable->clear();
}

template <typename TKey, typename TValue>
void LruCache<TKey, TValue>::attachToCache(Entry& entry) {
    if (mYoungest == NULL) {
        mYoungest = mOldest = &entry;
    } else {
        entry.parent = mYoungest;
        mYoungest->child = &entry;
        mYoungest = &entry;
    }
}

template <typename TKey, typename TValue>
void LruCache<TKey, TValue>::detachFromCache(Entry& entry) {
    if (entry.parent != NULL) {
        entry.parent->child = entry.child;
    } else {
        mOldest = entry.child;
    }
    if (entry.child != NULL) {
        entry.child->parent = entry.parent;
    } else {
        mYoungest = entry.parent;
    }

    entry.parent = NULL;
    entry.child = NULL;
}

}

#endif // ANDROID_UTILS_LRU_CACHE_H
