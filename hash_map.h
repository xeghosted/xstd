// ============================================================================
//  xstd/hash_map.h
//  ---------------------------------------------------------------------------
//  Open-addressing hash map. Linear probing, power-of-two capacity, separate
//  state byte per slot (no hash-sentinel hacks). Heap-backed via malloc/free.
//
//  Provides:
//    - xstd::hash<T>                          default hashers
//        specializations: int, unsigned int, __int64, unsigned __int64,
//                         T* (any pointer), string_view, string
//
//    - xstd::hash_map<K, V, Hash = hash<K> >
//        hash_map()                                   empty (no allocation)
//        hash_map(const hash_map&)                    deep copy
//        hash_map(const detail::hash_map_move_proxy<K,V,Hash>&)
//        ~hash_map()
//        hash_map& operator=(const hash_map&)
//
//        unsigned int size()     const
//        unsigned int capacity() const
//        bool         empty() const
//
//        bool         insert          (const K&, const V&)   false if key exists or OOM
//        bool         insert_or_assign(const K&, const V&)   false only on OOM
//        bool         contains(const K&) const
//        V*           find    (const K&)                     null if not found
//        const V*     find    (const K&) const
//        V&           operator[](const K&)                   inserts default V() if missing
//        bool         erase(const K&)                        true if removed
//        void         clear()
//        bool         reserve(unsigned int min_capacity)     false on OOM
//
//        iterator     begin();   iterator end();
//        const_iterator begin() const;   const_iterator end() const;
//
//    - xstd::move(hash_map<K,V,Hash>&) -> detail::hash_map_move_proxy<K,V,Hash>
//
//  Iterator:
//        iter.key()      -> const K&
//        iter.value()    -> V& / const V&
//        ++iter          advances, skipping empty/tombstone slots
//        iter == other / iter != other
//
//  Notes:
//    - operator[] requires V to be default-constructible.
//    - Load factor capped at 0.75 (size + tombstones). Growth doubles cap.
//    - Tombstones are recycled on insert and burned off by re-hash on growth.
//    - Integer hashes use Wellons' lowbias32 mix — cheap avalanche, no
//      pathological identity-hash behavior on contiguous keys.
//    - String hashes use FNV-1a 32-bit.
//    - OOM: insert / reserve / operator[] return false / leave state
//      untouched (operator[] returns a reference to a static dummy slot
//      so the caller still gets a writable V — see notes inside).
//      Observable via size() not advancing.
//    - K and V must have natural alignment <= 8 (malloc on XDK CRT is
//      8-byte aligned).
// ============================================================================

#ifndef XSTD_HASH_MAP_H
#define XSTD_HASH_MAP_H

#ifndef XSTD_INLINE
    #define XSTD_INLINE __forceinline
#endif

#ifndef XSTD_NULL
    #define XSTD_NULL 0
#endif

#include <new>
#include <stdlib.h>     // malloc / free
#include <string.h>     // memset
#include "assert.h"
#include "string_view.h"
#include "string.h"

namespace xstd {

// ---------- default hashers ------------------------------------------------

namespace detail {

XSTD_INLINE unsigned int lowbias32(unsigned int x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

XSTD_INLINE unsigned int fnv1a32(const char* p, unsigned int n) {
    unsigned int h = 0x811c9dc5u;
    for (unsigned int i = 0; i < n; ++i) {
        h ^= (unsigned int)(unsigned char)p[i];
        h *= 0x01000193u;
    }
    return h;
}

} // namespace detail

template <typename T> struct hash; // primary template, no body — must specialize

template <> struct hash<int> {
    XSTD_INLINE unsigned int operator()(int v) const { return detail::lowbias32((unsigned int)v); }
};
template <> struct hash<unsigned int> {
    XSTD_INLINE unsigned int operator()(unsigned int v) const { return detail::lowbias32(v); }
};
template <> struct hash<__int64> {
    XSTD_INLINE unsigned int operator()(__int64 v) const {
        unsigned __int64 u = (unsigned __int64)v;
        return detail::lowbias32((unsigned int)(u ^ (u >> 32)));
    }
};
template <> struct hash<unsigned __int64> {
    XSTD_INLINE unsigned int operator()(unsigned __int64 v) const {
        return detail::lowbias32((unsigned int)(v ^ (v >> 32)));
    }
};
template <typename T> struct hash<T*> {
    XSTD_INLINE unsigned int operator()(T* p) const {
        return detail::lowbias32((unsigned int)((unsigned __int64)p >> 3));
    }
};
template <> struct hash<string_view> {
    XSTD_INLINE unsigned int operator()(string_view sv) const {
        return detail::fnv1a32(sv.data(), sv.size());
    }
};
template <> struct hash<string> {
    XSTD_INLINE unsigned int operator()(const string& s) const {
        return detail::fnv1a32(s.data(), s.size());
    }
};

// ---------- hash_map -------------------------------------------------------

template <typename K, typename V, typename Hash> class hash_map;

namespace detail {

enum hash_map_slot {
    hash_map_slot_empty     = 0,
    hash_map_slot_occupied  = 1,
    hash_map_slot_tombstone = 2
};

template <typename K, typename V, typename Hash>
struct hash_map_move_proxy {
    hash_map<K, V, Hash>* src;
    XSTD_INLINE explicit hash_map_move_proxy(hash_map<K, V, Hash>& s) : src(&s) {}
};

} // namespace detail

template <typename K, typename V, typename Hash> detail::hash_map_move_proxy<K, V, Hash> move(hash_map<K, V, Hash>& m);

template <typename K, typename V, typename Hash = hash<K> >
class hash_map {
public:
    XSTD_INLINE hash_map()
        : states_(XSTD_NULL), keys_(XSTD_NULL), values_(XSTD_NULL),
          cap_(0), size_(0), tomb_(0) {}

    XSTD_INLINE hash_map(const hash_map& other)
        : states_(XSTD_NULL), keys_(XSTD_NULL), values_(XSTD_NULL),
          cap_(0), size_(0), tomb_(0) {
        copy_from(other);
    }

    XSTD_INLINE hash_map(const detail::hash_map_move_proxy<K, V, Hash>& p)
        : states_(p.src->states_), keys_(p.src->keys_), values_(p.src->values_),
          cap_(p.src->cap_), size_(p.src->size_), tomb_(p.src->tomb_) {
        p.src->states_ = XSTD_NULL;
        p.src->keys_   = XSTD_NULL;
        p.src->values_ = XSTD_NULL;
        p.src->cap_    = 0;
        p.src->size_   = 0;
        p.src->tomb_   = 0;
    }

    XSTD_INLINE ~hash_map() {
        destroy_storage();
    }

    XSTD_INLINE hash_map& operator=(const hash_map& other) {
        if (this == &other) return *this;
        clear();
        copy_from(other);
        return *this;
    }

    XSTD_INLINE unsigned int size()     const { return size_; }
    XSTD_INLINE unsigned int capacity() const { return cap_; }
    XSTD_INLINE bool         empty() const { return size_ == 0; }

    XSTD_INLINE bool contains(const K& k) const {
        return find_idx(k) != (unsigned int)-1;
    }

    XSTD_INLINE V* find(const K& k) {
        unsigned int i = find_idx(k);
        return i == (unsigned int)-1 ? (V*)XSTD_NULL : values_ + i;
    }
    XSTD_INLINE const V* find(const K& k) const {
        unsigned int i = find_idx(k);
        return i == (unsigned int)-1 ? (const V*)XSTD_NULL : values_ + i;
    }

    XSTD_INLINE bool insert(const K& k, const V& v) {
        if (!ensure_capacity_for_one()) return false;
        unsigned int idx;
        if (locate_for_insert(k, &idx)) return false; // already present
        place(idx, k, v);
        return true;
    }

    XSTD_INLINE bool insert_or_assign(const K& k, const V& v) {
        if (!ensure_capacity_for_one()) return false;
        unsigned int idx;
        if (locate_for_insert(k, &idx)) {
            values_[idx] = v;
            return true;
        }
        place(idx, k, v);
        return true;
    }

    XSTD_INLINE V& operator[](const K& k) {
        if (!ensure_capacity_for_one()) return dummy();
        unsigned int idx;
        if (locate_for_insert(k, &idx)) return values_[idx];
        place_default(idx, k);
        return values_[idx];
    }

    XSTD_INLINE bool erase(const K& k) {
        unsigned int i = find_idx(k);
        if (i == (unsigned int)-1) return false;
        keys_[i].~K();
        values_[i].~V();
        states_[i] = detail::hash_map_slot_tombstone;
        --size_;
        ++tomb_;
        return true;
    }

    XSTD_INLINE void clear() {
        if (!states_) return;
        for (unsigned int i = 0; i < cap_; ++i) {
            if (states_[i] == detail::hash_map_slot_occupied) {
                keys_[i].~K();
                values_[i].~V();
            }
            states_[i] = detail::hash_map_slot_empty;
        }
        size_ = 0;
        tomb_ = 0;
    }

    XSTD_INLINE bool reserve(unsigned int min_capacity) {
        // size + tomb must stay <= 0.75 * cap; pick smallest pow2 cap that fits.
        unsigned int needed_cap = 1;
        while (needed_cap < 16) needed_cap <<= 1;
        while ((unsigned __int64)min_capacity * 4 > (unsigned __int64)needed_cap * 3) {
            needed_cap <<= 1;
            if (needed_cap == 0) return false; // overflow
        }
        if (needed_cap <= cap_) return true;
        return rehash(needed_cap);
    }

    // ---------- iterators ----------
    class const_iterator;
    class iterator {
    public:
        XSTD_INLINE iterator() : map_(XSTD_NULL), idx_(0) {}
        XSTD_INLINE iterator(hash_map* m, unsigned int i) : map_(m), idx_(i) { skip(); }
        XSTD_INLINE const K& key()   const { return map_->keys_[idx_]; }
        XSTD_INLINE V&       value() const { return map_->values_[idx_]; }
        XSTD_INLINE iterator& operator++() { ++idx_; skip(); return *this; }
        XSTD_INLINE bool operator==(const iterator& o) const { return idx_ == o.idx_; }
        XSTD_INLINE bool operator!=(const iterator& o) const { return idx_ != o.idx_; }
    private:
        XSTD_INLINE void skip() {
            while (idx_ < map_->cap_ && map_->states_[idx_] != detail::hash_map_slot_occupied) ++idx_;
        }
        hash_map*    map_;
        unsigned int idx_;
        friend class const_iterator;
    };

    class const_iterator {
    public:
        XSTD_INLINE const_iterator() : map_(XSTD_NULL), idx_(0) {}
        XSTD_INLINE const_iterator(const hash_map* m, unsigned int i) : map_(m), idx_(i) { skip(); }
        XSTD_INLINE const K& key()   const { return map_->keys_[idx_]; }
        XSTD_INLINE const V& value() const { return map_->values_[idx_]; }
        XSTD_INLINE const_iterator& operator++() { ++idx_; skip(); return *this; }
        XSTD_INLINE bool operator==(const const_iterator& o) const { return idx_ == o.idx_; }
        XSTD_INLINE bool operator!=(const const_iterator& o) const { return idx_ != o.idx_; }
    private:
        XSTD_INLINE void skip() {
            while (idx_ < map_->cap_ && map_->states_[idx_] != detail::hash_map_slot_occupied) ++idx_;
        }
        const hash_map* map_;
        unsigned int    idx_;
    };

    XSTD_INLINE iterator       begin()       { return iterator(this, 0); }
    XSTD_INLINE iterator       end()         { return iterator(this, cap_); }
    XSTD_INLINE const_iterator begin() const { return const_iterator(this, 0); }
    XSTD_INLINE const_iterator end()   const { return const_iterator(this, cap_); }

private:
    XSTD_INLINE bool ensure_capacity_for_one() {
        // need: (size + tomb + 1) * 4 <= cap * 3
        unsigned __int64 wanted = (unsigned __int64)(size_ + tomb_ + 1) * 4;
        unsigned __int64 budget = (unsigned __int64)cap_ * 3;
        if (wanted <= budget) return true;
        unsigned int new_cap = cap_ ? cap_ * 2 : 16;
        return rehash(new_cap);
    }

    // Returns true if key already present at *out_idx (occupied). Otherwise
    // *out_idx is the slot to place into (empty or recycled tombstone).
    XSTD_INLINE bool locate_for_insert(const K& k, unsigned int* out_idx) const {
        unsigned int mask = cap_ - 1;
        unsigned int h    = Hash()(k);
        unsigned int i    = h & mask;
        unsigned int first_tomb = (unsigned int)-1;
        for (;;) {
            unsigned char st = states_[i];
            if (st == detail::hash_map_slot_empty) {
                *out_idx = (first_tomb != (unsigned int)-1) ? first_tomb : i;
                return false;
            }
            if (st == detail::hash_map_slot_tombstone) {
                if (first_tomb == (unsigned int)-1) first_tomb = i;
            } else { // occupied
                if (keys_[i] == k) { *out_idx = i; return true; }
            }
            i = (i + 1) & mask;
        }
    }

    XSTD_INLINE unsigned int find_idx(const K& k) const {
        if (cap_ == 0) return (unsigned int)-1;
        unsigned int mask = cap_ - 1;
        unsigned int h    = Hash()(k);
        unsigned int i    = h & mask;
        for (;;) {
            unsigned char st = states_[i];
            if (st == detail::hash_map_slot_empty) return (unsigned int)-1;
            if (st == detail::hash_map_slot_occupied && keys_[i] == k) return i;
            i = (i + 1) & mask;
        }
    }

    XSTD_INLINE void place(unsigned int idx, const K& k, const V& v) {
        bool was_tomb = (states_[idx] == detail::hash_map_slot_tombstone);
        new (keys_   + idx) K(k);
        new (values_ + idx) V(v);
        states_[idx] = detail::hash_map_slot_occupied;
        ++size_;
        if (was_tomb) --tomb_;
    }

    XSTD_INLINE void place_default(unsigned int idx, const K& k) {
        bool was_tomb = (states_[idx] == detail::hash_map_slot_tombstone);
        new (keys_   + idx) K(k);
        new (values_ + idx) V();
        states_[idx] = detail::hash_map_slot_occupied;
        ++size_;
        if (was_tomb) --tomb_;
    }

    XSTD_INLINE bool rehash(unsigned int new_cap) {
        // new_cap must be power-of-two and >= 16
        XSTD_ASSERT((new_cap & (new_cap - 1)) == 0);
        XSTD_ASSERT(new_cap >= 16);

        unsigned char* ns = (unsigned char*)malloc(new_cap);
        K*             nk = (K*)malloc(sizeof(K) * new_cap);
        V*             nv = (V*)malloc(sizeof(V) * new_cap);
        if (!ns || !nk || !nv) {
            if (ns) free(ns);
            if (nk) free(nk);
            if (nv) free(nv);
            return false;
        }
        memset(ns, detail::hash_map_slot_empty, new_cap);

        // re-insert live elements into the new arrays
        unsigned int new_size = 0;
        unsigned int mask = new_cap - 1;
        for (unsigned int i = 0; i < cap_; ++i) {
            if (states_[i] != detail::hash_map_slot_occupied) continue;
            unsigned int h = Hash()(keys_[i]);
            unsigned int j = h & mask;
            while (ns[j] == detail::hash_map_slot_occupied) j = (j + 1) & mask;
            new (nk + j) K(keys_[i]);
            new (nv + j) V(values_[i]);
            ns[j] = detail::hash_map_slot_occupied;
            ++new_size;
        }

        destroy_storage();
        states_ = ns;
        keys_   = nk;
        values_ = nv;
        cap_    = new_cap;
        size_   = new_size;
        tomb_   = 0;
        return true;
    }

    XSTD_INLINE void destroy_storage() {
        if (!states_) return;
        for (unsigned int i = 0; i < cap_; ++i) {
            if (states_[i] == detail::hash_map_slot_occupied) {
                keys_[i].~K();
                values_[i].~V();
            }
        }
        free(states_);
        free(keys_);
        free(values_);
        states_ = XSTD_NULL;
        keys_   = XSTD_NULL;
        values_ = XSTD_NULL;
        cap_    = 0;
        size_   = 0;
        tomb_   = 0;
    }

    XSTD_INLINE void copy_from(const hash_map& other) {
        if (other.size_ == 0) return;
        if (!reserve(other.size_)) return;
        for (unsigned int i = 0; i < other.cap_; ++i) {
            if (other.states_[i] == detail::hash_map_slot_occupied) {
                insert(other.keys_[i], other.values_[i]);
            }
        }
    }

    // OOM fallback for operator[] — returns a reference to a never-stored
    // dummy V so the caller's code path doesn't have to null-check. Writes
    // go to the dummy and are silently lost; same posture as vector OOM.
    XSTD_INLINE V& dummy() {
        static V dummy_v;
        return dummy_v;
    }

    unsigned char* states_;
    K*             keys_;
    V*             values_;
    unsigned int   cap_;
    unsigned int   size_;
    unsigned int   tomb_;

    template <typename Kx, typename Vx, typename Hx>
    friend detail::hash_map_move_proxy<Kx, Vx, Hx> move(hash_map<Kx, Vx, Hx>& m);
};

template <typename K, typename V, typename Hash>
XSTD_INLINE detail::hash_map_move_proxy<K, V, Hash> move(hash_map<K, V, Hash>& m) {
    return detail::hash_map_move_proxy<K, V, Hash>(m);
}

} // namespace xstd

#endif // XSTD_HASH_MAP_H
