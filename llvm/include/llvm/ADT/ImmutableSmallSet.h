//===- ImmutableSmallSet.h - sadas ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  asdas
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_IMMUTABLESMALLSET_H
#define LLVM_ADT_IMMUTABLESMALLSET_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/FoldingSet.h"
#include "llvm/ADT/ImmutableSet.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Recycler.h"
#include <iterator>
#include <memory>
#include <type_traits>

namespace llvm {

namespace detail {

template <class T, unsigned S> struct Block {
  using RawDataTy = typename std::aligned_storage<sizeof(T), alignof(T)>::type;
  using IndexTy = uint8_t;

  RawDataTy Data[S];
  unsigned RefCount = 0;
  IndexTy Occupied = 0;

  LLVM_NODISCARD T *at(IndexTy Index) {
    assert(Index <= S && "Out of boundaries access!");
    return reinterpret_cast<T *>(&Data[Index]);
  }

  LLVM_NODISCARD T *begin() { return at(0); }
  LLVM_NODISCARD T *end() { return at(Occupied); }

  void destroyTail(IndexTy NumberOfElementsToDestroy) {
    assert(NumberOfElementsToDestroy <= Occupied &&
           "Can't destroy more elements than the block has stored!");

    IndexTy NewOccupied = Occupied - NumberOfElementsToDestroy;
    std::for_each(at(NewOccupied), end(),
                  [](T &ElementToDestruct) { ElementToDestruct.~T(); });
    Occupied = NewOccupied;
  }

  void storeAt(const T &Value, IndexTy Index) {
    T *Loc = at(Index);
    Loc->~T();
    new (Loc) T(Value);
  }

  void push(const T &Value) {
    void *Buffer = &Data[Occupied++];
    new (Buffer) T(Value);
  }
};

template <class T, class Info, unsigned S> class SmallSetVector {
public:
  using value_type = typename Info::value_type;
  using value_type_ref = typename Info::value_type_ref;
  using key_type_ref = typename Info::key_type_ref;

  using ElementsTy = ArrayRef<std::remove_const_t<value_type>>;
  using BlockTy = Block<std::remove_const_t<value_type>, S>;
  using IndexTy = typename BlockTy::IndexTy;
  using iterator = typename ElementsTy::const_iterator;

  iterator begin() const { return getElements().begin(); }
  iterator end() const { return getElements().end(); }

  void retain() { ++RefCount; }
  void release() {
    assert(RefCount > 0 && "Reference count is already zero.");
    if (--RefCount == 0) {
      Owner.removeFromCache(this);
      free();
    }
  }

  size_t size() const { return End - Start; }

  LLVM_NODISCARD ElementsTy getElements() const {
    return {Memory.at(Start), Memory.at(End)};
  }

  LLVM_NODISCARD const value_type *find(key_type_ref K) const {
    const value_type *It = Memory.at(Start), *E = Memory.at(End);
    do {
      if (Info::isEqual(Info::KeyOfValue(*It), K))
        return It;
    } while (++It != E);
    return nullptr;
  }

  LLVM_NODISCARD const value_type *lookup(key_type_ref K) const {
    return find(K);
  }

  LLVM_NODISCARD bool contains(key_type_ref K) const {
    return find(K) != nullptr;
  }

  LLVM_NODISCARD bool isEqual(const SmallSetVector &Other) const {
    return this == &Other ||
           (size() == Other.size() && Hash == Other.Hash &&
            llvm::all_of(getElements(), [Other](value_type_ref Element) {
              return Other.containsValue(Element);
            }));
  }

  LLVM_NODISCARD bool isEqual(const ImutAVLTree<Info> &Other) const {
    size_t NumberOfFound = 0;
    bool FoundAll = llvm::all_of(
        Other, [&NumberOfFound, this](const ImutAVLTree<Info> &SubTree) {
          if (++NumberOfFound > size())
            return false;
          return containsValue(SubTree.getValue());
        });
    return FoundAll && NumberOfFound == size();
  }

  void Profile(llvm::FoldingSetNodeID &ID) { Profile(ID, getElements()); }

  class Factory {
  public:
    Factory()
        : Allocator(reinterpret_cast<uintptr_t>(new BumpPtrAllocator())) {}

    Factory(BumpPtrAllocator &Alloc)
        : Allocator(reinterpret_cast<uintptr_t>(&Alloc) | 0x1) {}

    ~Factory() {
      BlockRecycler.clear(getAllocator());
      SetRecycler.clear(getAllocator());
      if (ownsAllocator())
        delete &getAllocator();
    }

    LLVM_NODISCARD SmallSetVector *add(SmallSetVector *Origin,
                                       value_type_ref V) {

      if (Origin == getEmptySet()) {

        BlockTy *NewBlock = createBlock();
        NewBlock->push(V);
        return cached(createSmallSetVector(0, 1, *NewBlock, getElementHash(V)));
      }
      assert(Origin->size() < S &&
             "Small set is already at its full capacity!");

      const value_type *It = Origin->find(Info::KeyOfValue(V));
      unsigned Hash = adjustHash(Origin->Hash, getElementHash(V));

      const ElementsTy Elements = Origin->getElements();
      if (It != nullptr) {
        if (Info::isDataEqual(Info::DataOfValue(V), Info::DataOfValue(*It))) {
          return Origin;
        }

        // Replacing existing element
        const IndexTy Index = It - Elements.begin();
        Hash = adjustHash(Hash, getElementHash(*It));

        if (Origin->isOnlyCopy()) {
          removeFromCache(Origin);
          Origin->replaceAtIndex(Index, V);
          Origin->shrinkToFit();
          Origin->Hash = Hash;
          return cached(Origin);
        }

        BlockTy *NewBlock = createBlock();
        copyElements(Elements.take_front(Index), *NewBlock);
        NewBlock->push(V);
        copyElements(Elements.drop_front(Index + 1), *NewBlock);
        return cached(createSmallSetVector(0, Origin->size(), *NewBlock, Hash));
      }
      // Adding brand new element
      if (Origin->isOnlyCopy()) {
        // It's going to die after this function and we can freely mutate
        // and reuse it.
        removeFromCache(Origin);
        Origin->shrinkToFit();
        Origin->Memory.push(V);
        ++Origin->End;
        Origin->Hash = Hash;
        return cached(Origin);
      }
      if (Origin->atTheEnd() && Origin->Memory.Occupied != S) {
        // can add one more at the end
        Origin->Memory.push(V);
        return cached(createSmallSetVector(Origin->Start, Origin->End + 1,
                                           Origin->Memory, Hash));
      }
      BlockTy *NewBlock = createBlock();
      copyElements(Elements, *NewBlock);
      NewBlock->push(V);
      return cached(
          createSmallSetVector(0, Origin->size() + 1, *NewBlock, Hash));
    }

    LLVM_NODISCARD SmallSetVector *remove(SmallSetVector *Origin,
                                          key_type_ref K) {
      if (Origin == getEmptySet())
        return Origin;

      const ElementsTy Elements = Origin->getElements();

      const value_type *It = Origin->find(K);

      if (It == nullptr)
        return Origin;

      if (Origin->size() == 1)
        return getEmptySet();

      const IndexTy Index = It - Elements.begin();
      unsigned Hash = adjustHash(Origin->Hash, getElementHash(*It));

      if (Index == 0)
        return cached(createSmallSetVector(Origin->Start + 1, Origin->End,
                                           Origin->Memory, Hash));

      if (Index == Origin->End - Origin->Start - 1)
        return cached(createSmallSetVector(Origin->Start, Origin->End - 1,
                                           Origin->Memory, Hash));

      if (Origin->isOnlyCopy()) {
        removeFromCache(Origin);
        Origin->removeAtIndex(Index);
        Origin->shrinkToFit();
        Origin->Hash = Hash;
        return cached(Origin);
      }
      BlockTy *NewBlock = createBlock();
      copyElements(Elements.take_front(Index), *NewBlock);
      copyElements(Elements.drop_front(Index + 1), *NewBlock);
      return cached(
          createSmallSetVector(0, Origin->size() - 1, *NewBlock, Hash));
    }

    LLVM_NODISCARD SmallSetVector *getEmptySet() { return nullptr; }

    BumpPtrAllocator &getAllocator() const {
      return *reinterpret_cast<BumpPtrAllocator *>(Allocator & ~0x1);
    }

    void deallocate(BlockTy *B) { BlockRecycler.Deallocate(getAllocator(), B); }
    void deallocate(SmallSetVector *Set) {
      SetRecycler.Deallocate(getAllocator(), Set);
    }

    void removeFromCache(SmallSetVector *ToRemove) {
      if (ToRemove->Next)
        ToRemove->Next->Prev = ToRemove->Prev;

      if (ToRemove->Prev)
        ToRemove->Prev->Next = ToRemove->Next;
      else
        Cache[maskHash(ToRemove->Hash)] = ToRemove->Next;

      ToRemove->Next = nullptr;
      ToRemove->Prev = nullptr;
    }

  private:
    SmallSetVector *cached(SmallSetVector *ToCheck) {
      SmallSetVector *&Cached = Cache[maskHash(ToCheck->Hash)];

      if (Cached) {
        for (SmallSetVector *Candidate = Cached; Candidate != nullptr;
             Candidate = Candidate->Next) {

          if (ToCheck->isEqual(*Candidate)) {
            if (ToCheck->RefCount == 0)
              ToCheck->free();
            return Candidate;
          }
        }
        Cached->Prev = ToCheck;
        ToCheck->Next = Cached;
      }

      Cached = ToCheck;
      return ToCheck;
    }

    static unsigned maskHash(unsigned Hash) { return (Hash & ~0x02); }

    static unsigned getElementHash(value_type_ref Value) {
      llvm::FoldingSetNodeID ID;
      Info::Profile(ID, Value);
      return ID.ComputeHash();
    }

    static unsigned adjustHash(unsigned OriginalHash, unsigned AdjustmentHash) {
      return OriginalHash ^ AdjustmentHash;
    }

    bool verifyHash(SmallSetVector *ToVerify) const {
      unsigned CalculatedHash = 0;
      for (value_type_ref V : ToVerify->getElements()) {
        CalculatedHash = adjustHash(CalculatedHash, getElementHash(V));
      }
      return CalculatedHash == ToVerify->Hash;
    }

    void copyElements(ElementsTy Elements, BlockTy &To) {
      std::uninitialized_copy(Elements.begin(), Elements.end(), To.end());
      To.Occupied += Elements.size();
    }

    LLVM_NODISCARD BlockTy *createBlock() {
      void *Buffer = BlockRecycler.Allocate(getAllocator());
      return new (Buffer) BlockTy;
    }
    LLVM_NODISCARD SmallSetVector *createSmallSetVector(unsigned char Start,
                                                        unsigned char End,
                                                        BlockTy &Memory,
                                                        unsigned Hash) {
      void *Buffer = SetRecycler.Allocate(getAllocator());
      auto *Result =
          new (Buffer) SmallSetVector(Start, End, Memory, *this, Hash);
      assert(verifyHash(Result) && "Incorrect hash stored!");
      return Result;
    }

    LLVM_NODISCARD bool ownsAllocator() const { return (Allocator & 0x1) == 0; }

    Recycler<BlockTy> BlockRecycler;
    Recycler<SmallSetVector> SetRecycler;
    DenseMap<unsigned, SmallSetVector *> Cache;
    uintptr_t Allocator;
  };

private:
  SmallSetVector(IndexTy Start, IndexTy End, BlockTy &Memory, Factory &Owner,
                 unsigned Hash)
      : Start(Start), End(End), Memory(Memory), Owner(Owner), Hash(Hash) {
    ++Memory.RefCount;
  }

  void free() {
    assert(Memory.RefCount > 0 && "Reference count is already zero.");
    if (--Memory.RefCount == 0) {
      Owner.deallocate(&Memory);
    }
    Owner.deallocate(this);
  }

  static void Profile(FoldingSetNodeID &ID, ElementsTy Elements) {
    for (value_type_ref V : Elements) {
      Info::Profile(ID, V);
    }
  }

  LLVM_NODISCARD bool containsValue(value_type_ref Value) const {
    const value_type *It = find(Info::KeyOfValue(Value));
    return It &&
           Info::isDataEqual(Info::DataOfValue(*It), Info::DataOfValue(Value));
  }

  void shrinkToFit() {
    assert(isOnlyCopy() && "Shared blocks shouldn't be shrunk!");

    if (Start != 0) {
      // We need to shift everything to the left.
      ElementsTy Elements = getElements();
      // TODO: describe two possible cases here
      IndexTy Index = 0;
      for (value_type_ref Value : Elements.take_back(Start)) {
        Memory.storeAt(Value, Index++);
      }
    }

    Memory.destroyTail(Memory.Occupied - End);
    Start = 0;
    End = Memory.Occupied;
  }

  void removeAtIndex(IndexTy Index) {
    assert(isOnlyCopy() && "Shared blocks shouldn't be modified!");
    assert(Memory.Occupied > 1 && "Shouldn't remove the only element!");

    Memory.storeAt(*(Memory.end() - 1), Index);
    Memory.destroyTail(1);
    --End;
  }

  void replaceAtIndex(IndexTy Index, value_type_ref V) {
    assert(isOnlyCopy() && "Shared blocks shouldn't be modified!");
    Memory.storeAt(V, Index);
  }

  LLVM_NODISCARD bool atTheEnd() const { return End == Memory.Occupied; }

  LLVM_NODISCARD bool isOnlyCopy() const {
    assert(RefCount != 0);
    assert(Memory.RefCount != 0);
    return (RefCount | Memory.RefCount) == 1;
  }

  unsigned RefCount = 0;
  IndexTy Start, End;
  BlockTy &Memory;
  Factory &Owner;
  SmallSetVector *Prev = nullptr, *Next = nullptr;
  unsigned Hash;
};

template <class T, class Info, unsigned S>
using SmallSetVectorRef = llvm::IntrusiveRefCntPtr<SmallSetVector<T, Info, S>>;

} // end namespace detail

template <class ValT, unsigned S = 16, class ValInfo = ImutContainerInfo<ValT>>
class ImmutableSmallSet {
public:
  using value_type = typename ValInfo::value_type;
  using value_type_ref = typename ValInfo::value_type_ref;

  using SmallSet = detail::SmallSetVector<ValT, ValInfo, S>;
  using SmallPtrTy = SmallSet *;
  using LargeSet = ImmutableSet<ValT, ValInfo>;
  using LargePtrTy = typename LargeSet::TreeTy *;
  using RawPtrTy = void *;

  explicit ImmutableSmallSet(RawPtrTy Raw) {
    RawPtr = RawPtr.getFromOpaqueValue(Raw);
    retain();
  }

  ImmutableSmallSet(const ImmutableSmallSet &Other)
      : ImmutableSmallSet(Other.getRawPtrWithoutRetain()) {}
  ImmutableSmallSet &operator=(const ImmutableSmallSet &Other) {
    if (this != &Other) {
      release();
      RawPtr = Other.RawPtr;
      retain();
    }
    return *this;
  }

  ImmutableSmallSet(ImmutableSmallSet &&Other) : RawPtr(Other.RawPtr) {
    Other.RawPtr = nullptr;
  }
  ImmutableSmallSet &operator=(ImmutableSmallSet &&Other) {
    release();
    RawPtr = Other.RawPtr;
    Other.RawPtr = nullptr;
    return *this;
  }

  ~ImmutableSmallSet() { release(); }

  bool contains(value_type_ref V) const {
    if (LLVM_LIKELY(isSmall())) {
      SmallPtrTy ThisAsSmall = getSmall();
      return ThisAsSmall && ThisAsSmall->contains(V);
    }

    return getLarge().contains(V);
  }
  LLVM_NODISCARD bool isEmpty() const { return RawPtr.isNull(); }

  bool operator==(const ImmutableSmallSet &RHS) const {
    return RawPtr && RHS.RawPtr
               ? areEqual(*this, RHS)
               : getRawPtrWithoutRetain() == RHS.getRawPtrWithoutRetain();
  }
  bool operator!=(const ImmutableSmallSet &RHS) const {
    return !(*this == RHS);
  }
  bool operator<(const ImmutableSmallSet &RHS) const {
    if (isSmall() && RHS.isSmall())
      return getSmall()->size() < RHS.getSmall()->size();
    if (isSmall())
      return true;
    if (RHS.isSmall())
      return false;
    return getLarge().getHeight() < RHS.getLarge().getHeight();
  }

  RawPtrTy getRawPtr() {
    retain();
    return getRawPtrWithoutRetain();
  }

  RawPtrTy getRawPtrWithoutRetain() const { return RawPtr.getOpaqueValue(); }

  class iterator
      : public iterator_facade_base<iterator, std::bidirectional_iterator_tag,
                                    const value_type> {
  public:
    using Base = iterator_facade_base<iterator, std::bidirectional_iterator_tag,
                                      const value_type>;

    iterator() = default;
    explicit iterator(typename SmallSet::iterator It)
        : IsSmall(true), SmallIterator(It) {}
    explicit iterator(typename LargeSet::iterator It)
        : IsSmall(false), LargeIterator(It) {}

    iterator(const iterator &Other)
        : IsSmall(Other.IsSmall), SmallIterator(Other.SmallIterator),
          LargeIterator(Other.LargeIterator) {}
    iterator &operator=(const iterator &Other) {
      if (this != &Other) {
        IsSmall = Other.IsSmall;
        SmallIterator = Other.SmallIterator;
        LargeIterator = Other.LargeIterator;
      }
      return *this;
    }

    bool operator==(const iterator &Other) const {
      return IsSmall ? SmallIterator == Other.SmallIterator
                     : LargeIterator == Other.LargeIterator;
    }

    const value_type &operator*() const {
      return IsSmall ? *SmallIterator : *LargeIterator;
    }
    iterator &operator++() {
      if (IsSmall)
        ++SmallIterator;
      else
        ++LargeIterator;
      return *this;
    }
    iterator operator++(int Dummy) { return Base::operator++(Dummy); }
    iterator &operator--() {
      if (IsSmall)
        --SmallIterator;
      else
        --LargeIterator;
      return *this;
    }
    iterator operator--(int Dummy) { return Base::operator--(Dummy); }

  private:
    bool IsSmall = true;
    typename SmallSet::iterator SmallIterator;
    typename LargeSet::iterator LargeIterator;
  };

  iterator begin() const {
    if (RawPtr.isNull())
      return {};

    if (LLVM_LIKELY(isSmall()))
      return iterator{getSmall()->begin()};

    return iterator{getLarge().begin()};
  }

  iterator end() const {
    if (RawPtr.isNull())
      return {};

    if (LLVM_LIKELY(isSmall()))
      return iterator{getSmall()->end()};

    return iterator{getLarge().end()};
  }

  static void Profile(FoldingSetNodeID &ID, const ImmutableSmallSet &Set) {
    ID.AddPointer(Set.getRawPtrWithoutRetain());
  }

  void Profile(FoldingSetNodeID &ID) const { return Profile(ID, *this); }

  class Factory {
  public:
    Factory(BumpPtrAllocator &Alloc) : SmallF(Alloc), LargeF(Alloc) {}
    Factory() = default;

    Factory(const Factory &) = delete;
    void operator=(const Factory &) = delete;
    Factory(Factory &&) = delete;
    Factory &operator=(Factory &&) = delete;

    LLVM_NODISCARD ImmutableSmallSet getEmptySet() {
      return {static_cast<SmallPtrTy>(nullptr)};
    }
    LLVM_NODISCARD ImmutableSmallSet add(ImmutableSmallSet Origin,
                                         value_type_ref V) {
      if (LLVM_LIKELY(Origin.isSmall())) {
        SmallPtrTy OriginAsSmall = Origin.getSmall();
        if (LLVM_LIKELY(!OriginAsSmall || OriginAsSmall->size() < S)) {
          OriginAsSmall = SmallF.add(OriginAsSmall, V);
          OriginAsSmall->retain();
          return OriginAsSmall;
        }

        LargeSet EnlargedOrigin = LargeF.getEmptySet();
        llvm::for_each(OriginAsSmall->getElements(),
                       [this, &EnlargedOrigin](value_type_ref StoredInSmall) {
                         EnlargedOrigin =
                             LargeF.add(EnlargedOrigin, StoredInSmall);
                       });
        EnlargedOrigin = LargeF.add(EnlargedOrigin, V);
        return EnlargedOrigin.getRoot();
      }

      return LargeF.add(Origin.getLarge(), V).getRoot();
    }
    LLVM_NODISCARD ImmutableSmallSet remove(ImmutableSmallSet Origin,
                                            value_type_ref V) {
      if (LLVM_LIKELY(Origin.isSmall())) {
        SmallPtrTy Result = SmallF.remove(Origin.getSmall(), V);
        if (Result)
          Result->retain();
        return Result;
      }

      return LargeF.remove(Origin.getLarge(), V).getRoot();
    }

    BumpPtrAllocator &getAllocator() { return SmallF.getAllocator(); }

  private:
    typename SmallSet::Factory SmallF;
    typename LargeSet::Factory LargeF;
  };

private:
  ImmutableSmallSet(SmallPtrTy Impl) : RawPtr(Impl) {}
  ImmutableSmallSet(LargePtrTy Impl) : RawPtr(Impl) {}

  void retain() {
    if (RawPtr.isNull())
      return;

    if (LLVM_LIKELY(isSmall()))
      getSmall()->retain();
    else
      getLargePtr()->retain();
  }

  void release() {
    if (RawPtr.isNull())
      return;

    if (LLVM_LIKELY(isSmall()))
      getSmall()->release();
    else
      getLargePtr()->release();
  }

  SmallPtrTy getSmall() const { return RawPtr.template get<SmallPtrTy>(); }

  LargePtrTy getLargePtr() const { return RawPtr.template get<LargePtrTy>(); }

  LargeSet getLarge() const { return LargeSet(getLargePtr()); }

  static bool areEqual(const ImmutableSmallSet &LHS,
                       const ImmutableSmallSet &RHS) {
    switch (LHS.isSmall() + (RHS.isSmall() << 1)) {
    case 3: // both are small
      return LHS.getSmall()->isEqual(*RHS.getSmall());
    case 0: // both are large
      return LHS.getLarge() == RHS.getLarge();
    case 1:
      return LHS.getSmall()->isEqual(*RHS.getLargePtr());
    case 2:
      return RHS.getSmall()->isEqual(*LHS.getLargePtr());
    default:
      llvm_unreachable("Impossible result");
    }
  }

  LLVM_NODISCARD bool isSmall() const {
    return RawPtr.template is<SmallPtrTy>();
  }

  PointerUnion<SmallPtrTy, LargePtrTy> RawPtr;
};
} // end namespace llvm

#endif // LLVM_ADT_IMMUTABLESMALLSET_H
