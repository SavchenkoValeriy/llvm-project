//===- ImmutableHashSet.h - Immutable hash set interface --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
//  This file defines the ImmutableHashSet class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_IMMUTABLEHASHSET_H
#define LLVM_ADT_IMMUTABLEHASHSET_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/ImmutableSet.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/TrailingObjects.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#define DEBUG(X)

namespace llvm {

namespace detail {

using bits_t = std::uint32_t;
using size_t = std::size_t;
using bits_t = std::uint32_t;
using shift_t = std::uint32_t;
using count_t = std::uint32_t;
using hash_t = size_t;

constexpr count_t getNumberOfBranches(bits_t Bits) {
  return count_t{1u} << Bits;
}

constexpr hash_t getMask(bits_t Bits) {
  // Number of branches is always a power of 2.
  // This means that if we subtract 1, we get all lower bits set to 1, i.e. the
  // mask.
  return getNumberOfBranches(Bits) - 1u;
}

constexpr count_t getMaxDepth(bits_t Bits) {
  // We need to calculate how many chunks of Bits can fit into one hash_t
  return (sizeof(hash_t) * 8u + Bits - 1u) / Bits;
}

constexpr shift_t getMaxShift(bits_t Bits) { return getMaxDepth(Bits) * Bits; }
template <class ValueInfo> class HAMT {
  using value_type = typename ValueInfo::value_type;
  using const_value_type = typename ValueInfo::const_value_type;
  using value_type_ref = typename ValueInfo::value_type_ref;
  using const_value_type_ref = typename ValueInfo::const_value_type_ref;
  using key_type = typename ValueInfo::key_type;
  using key_type_ref = typename ValueInfo::key_type_ref;

  using BitmapType = std::uint32_t;

  // TODO: Change the type
  // TODO: Remove hardcode
  static constexpr unsigned Bits = 5;

  class Node;
  using NodePtr = Node *;

  class Node : public RefCountedBase<Node> {
    struct InnerNode final : public TrailingObjects<InnerNode, NodePtr> {
      BitmapType NodeMap;
      BitmapType DataMap;
      size_t Size;

      using TrailingType = NodePtr;
    };

    struct DataNode final : public TrailingObjects<DataNode, value_type> {
      size_t Size;

      using TrailingType = value_type;
    };

    union ImplType {
      InnerNode Inner;
      DataNode Data;
    };

    ImplType Impl;

  public:
    class Factory {
    public:
      Node *addCollision(NodePtr Original, const_value_type_ref NewElement) {
        DEBUG(llvm::errs()
              << "addCollision(NodePtr Original, const_value_type_ref "
                 "NewElement)\n");
        DataNode &DesugaredOriginal = Original->Impl.Data;
        const size_t NewSize = DesugaredOriginal.Size + 1;
        Node *NewNode = allocate<DataNode>(NewSize);

        DataNode &DesugaredNew = NewNode->Impl.Data;
        DesugaredNew.Size = NewSize;

        ArrayRef<value_type> OriginalCollisions = Original->getCollisions();
        std::uninitialized_copy(OriginalCollisions.begin(),
                                OriginalCollisions.end(),
                                NewNode->getCollisions().begin());
        new (&NewNode->getCollisions()[NewSize - 1]) value_type(NewElement);

        DEBUG(llvm::errs() << "New size: " << NewSize << "\n");

        return NewNode;
      }

      Node *replaceCollision(NodePtr Original, const_value_type_ref NewElement,
                             count_t Index) {
        DEBUG(llvm::errs()
              << "replaceCollision(NodePtr Original, "
                 "const_value_type_ref NewElement, count_t Index)\n");
        DataNode &DesugaredOriginal = Original->Impl.Data;
        Node *NewNode = allocate<DataNode>(DesugaredOriginal.Size);

        DataNode &DesugaredNew = NewNode->Impl.Data;
        DesugaredNew.Size = DesugaredOriginal.Size;

        ArrayRef<value_type> OriginalCollisions = Original->getCollisions();
        std::uninitialized_copy(OriginalCollisions.begin(),
                                OriginalCollisions.end(),
                                NewNode->getCollisions().begin());
        new (&NewNode->getCollisions()[Index]) value_type(NewElement);

        return NewNode;
      }

      Node *removeCollision(NodePtr Original, count_t Offset) {
        DataNode &DesugaredOriginal = Original->Impl.Data;
        assert(DesugaredOriginal.Size > 1 &&
               "Removing collisions from nodes with only 1 collision");

        const size_t NewSize = DesugaredOriginal.Size - 1;
        Node *NewNode = allocate<DataNode>(NewSize);

        DataNode &DesugaredNew = NewNode->Impl.Data;
        DesugaredNew.Size = NewSize;

        ArrayRef<value_type> OriginalCollisions = Original->getCollisions();
        MutableArrayRef<value_type> NewCollisions = NewNode->getCollisions();

        std::uninitialized_copy_n(OriginalCollisions.begin(), Offset,
                                  NewCollisions.begin());
        // Skip OriginalCollisions[Offset]
        std::uninitialized_copy(OriginalCollisions.begin() + Offset + 1,
                                OriginalCollisions.end(),
                                NewCollisions.begin() + Offset);

        return NewNode;
      }

      Node *replaceInnerNode(NodePtr Original, NodePtr NewChild,
                             count_t Offset) {
        assert(NewChild && "Nodes can't be null");
        DEBUG(llvm::errs()
              << "replaceInnerNode(NodePtr Original, NodePtr NewChild, "
                 "count_t Offset)\n");
        InnerNode &DesugaredOriginal = Original->Impl.Inner;
        Node *NewNode = allocate<InnerNode>(DesugaredOriginal.Size);

        InnerNode &DesugaredNew = NewNode->Impl.Inner;
        DesugaredNew.Size = DesugaredOriginal.Size;
        DesugaredNew.NodeMap = DesugaredOriginal.NodeMap;
        DesugaredNew.DataMap = DesugaredOriginal.DataMap;
        llvm::copy(Original->getAllChildren(),
                   NewNode->getAllChildren().begin());
        NewNode->getInnerChild(Offset) = NewChild;

        return NewNode;
      }

      Node *replaceDataNode(NodePtr Original, const_value_type_ref Element,
                            count_t Offset) {
        DEBUG(
            llvm::errs() << "replaceDataNode(NodePtr Original, "
                            "const_value_type_ref Element, count_t Offset)\n");
        Node *NewChild = createDataNode(Element);
        InnerNode &DesugaredOriginal = Original->Impl.Inner;
        Node *NewNode = allocate<InnerNode>(DesugaredOriginal.Size);

        InnerNode &DesugaredNew = NewNode->Impl.Inner;
        DesugaredNew.Size = DesugaredOriginal.Size;
        DesugaredNew.NodeMap = DesugaredOriginal.NodeMap;
        DesugaredNew.DataMap = DesugaredOriginal.DataMap;
        llvm::copy(Original->getAllChildren(),
                   NewNode->getAllChildren().begin());
        NewNode->getDataChild(Offset) = NewChild;

        return NewNode;
      }

      Node *mergeValues(shift_t Shift, const_value_type_ref First,
                        hash_t FirstHash, const_value_type_ref Second,
                        hash_t SecondHash) {
        DEBUG(
            llvm::errs()
            << "mergeValues(shift_t Shift, const_value_type_ref First, hash_t "
               "FirstHash, const_value_type_ref Second, hash_t SecondHash)\n");
        DEBUG(errs() << "Merging " << First << " and " << Second << "\n");
        if (LLVM_LIKELY(Shift < getMaxShift(Bits))) {
          hash_t ShiftedMask = getMask(Bits) << Shift;
          DEBUG(errs() << "Population " << countPopulation(ShiftedMask)
                       << ", shift " << Shift << "\n");
          hash_t FirstIndex = FirstHash & ShiftedMask;
          hash_t SecondIndex = SecondHash & ShiftedMask;
          DEBUG(errs() << "First " << (FirstIndex >> Shift) << ", Second "
                       << (SecondIndex >> Shift) << "\n");

          if (LLVM_UNLIKELY(FirstIndex == SecondIndex)) {
            Node *Merged =
                mergeValues(Shift + Bits, First, FirstHash, Second, SecondHash);
            return createInnerNode(FirstIndex >> Shift, Merged);
          }

          return createInnerNode(FirstIndex >> Shift, First,
                                 SecondIndex >> Shift, Second);
        }

        return createDataNode(First, Second);
      }

      Node *createDataNode(const_value_type_ref Data) {
        DEBUG(llvm::errs() << "createDataNode(const_value_type_ref Data)\n");
        constexpr size_t NewSize = 1;
        Node *NewNode = allocate<DataNode>(NewSize);

        DataNode &DesugaredNew = NewNode->Impl.Data;
        DesugaredNew.Size = NewSize;
        new (&NewNode->getCollisions()[0]) value_type(Data);

        return NewNode;
      }
      Node *createDataNode(const_value_type_ref First,
                           const_value_type_ref Second) {
        DEBUG(llvm::errs() << "createDataNode(const_value_type_ref First, "
                              "const_value_type_ref Second)\n");
        constexpr size_t NewSize = 2;
        Node *NewNode = allocate<DataNode>(NewSize);

        DataNode &DesugaredNew = NewNode->Impl.Data;
        DesugaredNew.Size = NewSize;
        new (&NewNode->getCollisions()[0]) value_type(First);
        new (&NewNode->getCollisions()[1]) value_type(Second);

        return NewNode;
      }
      Node *createInnerNode(count_t Index, const_value_type_ref Element) {
        DEBUG(llvm::errs() << "createInnerNode(count_t Index, "
                              "const_value_type_ref Element)\n");
        constexpr size_t NewSize = 1;
        Node *NewNode = allocate<InnerNode>(NewSize);
        Node *Child = createDataNode(Element);

        InnerNode &DesugaredNew = NewNode->Impl.Inner;
        DesugaredNew.Size = NewSize;
        DesugaredNew.NodeMap = 0;
        DesugaredNew.DataMap = BitmapType{1u} << Index;
        NewNode->getDataChild(0) = Child;

        return NewNode;
      }
      Node *createInnerNode(count_t Index, NodePtr Child) {
        DEBUG(
            llvm::errs() << "createInnerNode(count_t Index, NodePtr Child)\n");
        constexpr size_t NewSize = 1;
        Node *NewNode = allocate<InnerNode>(NewSize);

        InnerNode &DesugaredNew = NewNode->Impl.Inner;
        DesugaredNew.Size = NewSize;
        DesugaredNew.NodeMap = BitmapType{1u} << Index;
        DesugaredNew.DataMap = 0;
        NewNode->getInnerChild(0) = Child;

        return NewNode;
      }
      Node *createInnerNode(count_t FirstIndex, const_value_type_ref First,
                            count_t SecondIndex, const_value_type_ref Second) {
        DEBUG(llvm::errs()
              << "createInnerNode(count_t FirstIndex, const_value_type_ref "
                 "First, count_t SecondIndex, const_value_type_ref Second)\n");
        DEBUG(errs() << "Indices: " << FirstIndex << ", " << SecondIndex
                     << "\n");
        assert(FirstIndex != SecondIndex);
        constexpr size_t NewSize = 2;
        Node *NewNode = allocate<InnerNode>(NewSize);
        Node *FirstNode = createDataNode(First);
        Node *SecondNode = createDataNode(Second);

        InnerNode &DesugaredNew = NewNode->Impl.Inner;
        DesugaredNew.Size = NewSize;
        DesugaredNew.NodeMap = 0;
        DesugaredNew.DataMap =
            (BitmapType{1u} << FirstIndex) | (BitmapType{1u} << SecondIndex);
        if (FirstIndex < SecondIndex) {
          NewNode->getDataChild(0) = FirstNode;
          NewNode->getDataChild(1) = SecondNode;
        } else {
          NewNode->getDataChild(0) = SecondNode;
          NewNode->getDataChild(1) = FirstNode;
        }

        return NewNode;
      }

      Node *replaceInnerNodeWithData(NodePtr Original, NodePtr NewChild,
                                     count_t IndexBit, count_t NodeOffset) {
        assert(NewChild && "Nodes can't be null");

        InnerNode &DesugaredOriginal = Original->Impl.Inner;
        assert(DesugaredOriginal.NodeMap & IndexBit &&
               "Index bit should not correspond to data node");
        assert(!(DesugaredOriginal.DataMap & IndexBit) &&
               "Should have an inner node for the given index bit");
        Node *NewNode = allocate<InnerNode>(DesugaredOriginal.Size);

        InnerNode &DesugaredNew = NewNode->Impl.Inner;
        DesugaredNew.Size = DesugaredOriginal.Size;
        count_t DataOffset =
            countPopulation(DesugaredOriginal.DataMap & (IndexBit - 1));
        count_t CanonicalDataOffset =
            Original->getCanonicalDataOffset(DataOffset);

        // "Move" given index bit from nodemap...
        DesugaredNew.NodeMap = DesugaredOriginal.NodeMap ^ IndexBit;
        // ...to datamap
        DesugaredNew.DataMap = DesugaredOriginal.DataMap | IndexBit;

        ArrayRef<NodePtr> OriginalChildren = Original->getAllChildren();
        MutableArrayRef<NodePtr> NewChildren = NewNode->getAllChildren();

        std::copy_n(OriginalChildren.begin(), NodeOffset, NewChildren.begin());
        // Skip OriginalChildren[NodeOffset] because this is the
        // element we are replacing here.
        std::copy(OriginalChildren.begin() + NodeOffset + 1,
                  OriginalChildren.begin() + CanonicalDataOffset + 1,
                  NewChildren.begin() + NodeOffset);
        NewNode->getAllChildren()[CanonicalDataOffset] = NewChild;
        std::copy(OriginalChildren.begin() + CanonicalDataOffset + 1,
                  OriginalChildren.end(),
                  NewChildren.begin() + CanonicalDataOffset + 1);

        return NewNode;
      }

      Node *replaceDataNodeWithInner(NodePtr Original, NodePtr NewChild,
                                     count_t IndexBit, count_t DataOffset) {
        DEBUG(llvm::errs()
              << "replaceDataNodeWithInner(NodePtr Original, NodePtr "
                 "NewChild, count_t IndexBit, count_t DataOffset)\n");
        assert(NewChild && "Nodes can't be null");

        InnerNode &DesugaredOriginal = Original->Impl.Inner;
        assert(!(DesugaredOriginal.NodeMap & IndexBit) &&
               "Index bit should not correspond to inner node");
        assert(DesugaredOriginal.DataMap & IndexBit &&
               "Should have a data node for the given index bit");
        Node *NewNode = allocate<InnerNode>(DesugaredOriginal.Size);

        InnerNode &DesugaredNew = NewNode->Impl.Inner;
        DesugaredNew.Size = DesugaredOriginal.Size;
        count_t NodeOffset =
            countPopulation(DesugaredOriginal.NodeMap & (IndexBit - 1));
        count_t CanonicalDataOffset =
            Original->getCanonicalDataOffset(DataOffset);

        // "Move" given index bit from datamap...
        DesugaredNew.DataMap = DesugaredOriginal.DataMap ^ IndexBit;
        // ...to nodemap
        DesugaredNew.NodeMap = DesugaredOriginal.NodeMap | IndexBit;

        ArrayRef<NodePtr> OriginalChildren = Original->getAllChildren();
        MutableArrayRef<NodePtr> NewChildren = NewNode->getAllChildren();

        std::copy_n(OriginalChildren.begin(), NodeOffset, NewChildren.begin());
        NewNode->getInnerChild(NodeOffset) = NewChild;
        std::copy(OriginalChildren.begin() + NodeOffset,
                  OriginalChildren.begin() + CanonicalDataOffset,
                  NewChildren.begin() + NodeOffset + 1);
        // Skip OriginalChildren[CanonicalDataOffset] because this is the
        // element we are replacing here.
        std::copy(OriginalChildren.begin() + CanonicalDataOffset + 1,
                  OriginalChildren.end(),
                  NewChildren.begin() + CanonicalDataOffset + 1);

        return NewNode;
      }

      Node *addDataChild(NodePtr Original, count_t IndexBit,
                         const_value_type_ref Element) {
        DEBUG(
            llvm::errs() << "addDataChild(NodePtr Original, count_t IndexBit, "
                            "const_value_type_ref Element)\n");

        InnerNode &DesugaredOriginal = Original->Impl.Inner;
        const size_t NewSize = DesugaredOriginal.Size + 1;

        Node *NewNode = allocate<InnerNode>(NewSize);
        InnerNode &DesugaredNew = NewNode->Impl.Inner;
        DesugaredNew.Size = NewSize;
        DesugaredNew.NodeMap = DesugaredOriginal.NodeMap;
        DesugaredNew.DataMap = DesugaredOriginal.DataMap | IndexBit;

        count_t Offset =
            countPopulation(DesugaredOriginal.DataMap & (IndexBit - 1));
        count_t CanonicalOffset = NewNode->getCanonicalDataOffset(Offset);

        ArrayRef<NodePtr> OriginalChildren = Original->getAllChildren();
        MutableArrayRef<NodePtr> NewChildren = NewNode->getAllChildren();

        std::copy_n(OriginalChildren.begin(), CanonicalOffset,
                    NewChildren.begin());
        NewChildren[CanonicalOffset] = createDataNode(Element);
        std::copy(OriginalChildren.begin() + CanonicalOffset,
                  OriginalChildren.end(),
                  NewChildren.begin() + CanonicalOffset + 1);

        return NewNode;
      }

      Node *removeInnerNode(NodePtr Original, count_t IndexBit,
                            count_t Offset) {
        InnerNode &DesugaredOriginal = Original->Impl.Inner;
        assert(DesugaredOriginal.Size > 1 &&
               "Removing children from nodes of size 1 doesn't make sense");
        assert(DesugaredOriginal.NodeMap & IndexBit &&
               "Index bit should not correspond to data node");
        assert(!(DesugaredOriginal.DataMap & IndexBit) &&
               "Should have an inner node for the given index bit");

        const size_t NewSize = DesugaredOriginal.Size - 1;
        Node *NewNode = allocate<InnerNode>(NewSize);

        InnerNode &DesugaredNew = NewNode->Impl.Inner;
        DesugaredNew.Size = NewSize;
        DesugaredNew.NodeMap = DesugaredOriginal.NodeMap ^ IndexBit;
        DesugaredNew.DataMap = DesugaredOriginal.DataMap;

        ArrayRef<NodePtr> OriginalChildren = Original->getAllChildren();
        MutableArrayRef<NodePtr> NewChildren = NewNode->getAllChildren();

        std::copy_n(OriginalChildren.begin(), Offset, NewChildren.begin());
        std::copy(OriginalChildren.begin() + Offset + 1, OriginalChildren.end(),
                  NewChildren.begin() + Offset);

        return NewNode;
      }

      Node *removeDataChild(NodePtr Original, count_t IndexBit,
                            count_t Offset) {
        InnerNode &DesugaredOriginal = Original->Impl.Inner;
        assert(DesugaredOriginal.Size > 1 &&
               "Removing children from nodes of size 1 doesn't make sense");
        assert(!(DesugaredOriginal.NodeMap & IndexBit) &&
               "Index bit should not correspond to inner node");
        assert(DesugaredOriginal.DataMap & IndexBit &&
               "Should have a data node for the given index bit");

        const size_t NewSize = DesugaredOriginal.Size - 1;
        Node *NewNode = allocate<InnerNode>(NewSize);

        InnerNode &DesugaredNew = NewNode->Impl.Inner;
        DesugaredNew.Size = NewSize;
        DesugaredNew.NodeMap = DesugaredOriginal.NodeMap;
        DesugaredNew.DataMap = DesugaredOriginal.DataMap ^ IndexBit;
        count_t CanonicalOffset = Original->getCanonicalDataOffset(Offset);

        ArrayRef<NodePtr> OriginalChildren = Original->getAllChildren();
        MutableArrayRef<NodePtr> NewChildren = NewNode->getAllChildren();

        std::copy_n(OriginalChildren.begin(), CanonicalOffset,
                    NewChildren.begin());
        std::copy(OriginalChildren.begin() + CanonicalOffset + 1,
                  OriginalChildren.end(),
                  NewChildren.begin() + CanonicalOffset);

        return NewNode;
      }

      Factory()
          : Allocator(reinterpret_cast<uintptr_t>(new BumpPtrAllocator())) {}

      Factory(BumpPtrAllocator &Alloc)
          : Allocator(reinterpret_cast<uintptr_t>(&Alloc) | 0x1) {}

      ~Factory() {
        if (ownsAllocator())
          delete &getAllocator();
      }

    private:
      template <class NodeType> Node *allocate(size_t Size) {
        void *Buffer = getAllocator().Allocate(
            sizeof(Node) + NodeType::template additionalSizeToAlloc<
                               typename NodeType::TrailingType>(Size),
            alignof(Node));
        Node *Result = new (Buffer) Node();
        new (&Result->Impl) NodeType();
        return Result;
      }

      bool ownsAllocator() const { return (Allocator & 0x1) == 0; }

      BumpPtrAllocator &getAllocator() const {
        return *reinterpret_cast<BumpPtrAllocator *>(Allocator & ~0x1);
      }

      uintptr_t Allocator;

      BumpPtrAllocator Arena;
    };

    BitmapType getNodeMap() const { return Impl.Inner.NodeMap; }
    BitmapType getDataMap() const { return Impl.Inner.DataMap; }
    MutableArrayRef<NodePtr> getAllChildren() {
      return {Impl.Inner.template getTrailingObjects<NodePtr>(),
              getNumberOfChildren()};
    }

    MutableArrayRef<NodePtr> getInnerChildren() {
      return getAllChildren().slice(0, countPopulation(getNodeMap()));
    }

    NodePtr &getInnerChild(count_t Offset) { return getAllChildren()[Offset]; }

    NodePtr &getDataChild(count_t Offset) {
      return getAllChildren()[getCanonicalDataOffset(Offset)];
    }

    const_value_type_ref getData(count_t Offset) {
      return getDataChild(Offset)->getCollisions()[0];
    }

    count_t getCanonicalDataOffset(count_t Offset) const {
      return getNumberOfChildren() - 1 - Offset;
    }

    size_t getNumberOfChildren() const { return Impl.Inner.Size; }

    MutableArrayRef<value_type> getCollisions() {
      return {Impl.Data.template getTrailingObjects<value_type>(),
              Impl.Data.Size};
    }
  };

  NodePtr Root;

  enum class RemoveKind { None, Modified, Trivial, Removed };

public:
  class Factory {
  public:
    LLVM_NODISCARD HAMT getEmptySet() { return {nullptr}; }
    LLVM_NODISCARD HAMT add(HAMT Trie, const_value_type_ref Element) {
      hash_t Hash = ValueInfo::getHash(Element);
      auto Result = addImpl(Trie.Root, Element, Hash);
      return {Result.first};
    }
    LLVM_NODISCARD HAMT remove(HAMT Trie, key_type_ref Element) {
      if (Trie.isEmpty())
        return Trie;

      hash_t Hash = ValueInfo::getHash(Element);
      auto Result = removeImpl(Trie.Root, Element, Hash);
      assert(Result.second != RemoveKind::Trivial &&
             "Simplification of trivial paths should be finished before "
             "finalizing the result");
      if (Result.second == RemoveKind::None)
        return Trie;

      return {Result.first};
    }

    Factory() = default;
    Factory(BumpPtrAllocator &Alloc) : NodeFactory(Alloc) {}

  private:
    LLVM_NODISCARD std::pair<NodePtr, bool>
    addImpl(NodePtr Node, const_value_type_ref Element, hash_t Hash,
            shift_t Shift = 0) {
      if (LLVM_UNLIKELY(Shift == getMaxShift(Bits))) {
        ArrayRef<value_type> Collisions = Node->getCollisions();

        for (count_t Index = 0; Index < Collisions.size(); ++Index)
          if (ValueInfo::areEqual(Collisions[Index], Element))
            return {NodeFactory.replaceCollision(Node, Element, Index), false};

        return {NodeFactory.addCollision(Node, Element), true};
      }

      hash_t ShiftedMask = getMask(Bits) << Shift;
      count_t Index = (Hash & ShiftedMask) >> Shift;

      if (Node == nullptr) {
        return {NodeFactory.createInnerNode(Index, Element), true};
      }

      count_t IndexBit = BitmapType{1u} << Index;

      if (Node->getNodeMap() & IndexBit) {
        count_t Offset = countPopulation(Node->getNodeMap() & (IndexBit - 1));
        auto Result =
            addImpl(Node->getInnerChild(Offset), Element, Hash, Shift + Bits);
        Result.first = NodeFactory.replaceInnerNode(Node, Result.first, Offset);
        return Result;
      }

      if (Node->getDataMap() & IndexBit) {
        count_t Offset = countPopulation(Node->getDataMap() & (IndexBit - 1));
        const_value_type_ref StoredValue = Node->getData(Offset);
        if (ValueInfo::areEqual(StoredValue, Element))
          return {NodeFactory.replaceDataNode(Node, Element, Offset), false};

        NodePtr NewChild =
            NodeFactory.mergeValues(Shift + Bits, Element, Hash, StoredValue,
                                    ValueInfo::getHash(StoredValue));
        return {NodeFactory.replaceDataNodeWithInner(Node, NewChild, IndexBit,
                                                     Offset),
                true};
      }
      return {NodeFactory.addDataChild(Node, IndexBit, Element), true};
    }

    LLVM_NODISCARD std::pair<NodePtr, RemoveKind>
    removeImpl(NodePtr Node, key_type_ref Element, hash_t Hash,
               shift_t Shift = 0) {
      if (LLVM_UNLIKELY(Shift == getMaxShift(Bits))) {
        ArrayRef<value_type> Collisions = Node->getCollisions();

        for (count_t Index = 0; Index < Collisions.size(); ++Index)
          if (ValueInfo::areEqual(Collisions[Index], Element)) {
            RemoveKind Kind = RemoveKind::Modified;
            switch (Collisions.size()) {
            case 1:
              // The whole node has been removed
              return {nullptr, RemoveKind::Removed};
            case 2:
              // There are no collisions anymore, this node is now not
              // restricted to be at the very bottom.
              Kind = RemoveKind::Trivial;
              LLVM_FALLTHROUGH;
            default:
              return {NodeFactory.removeCollision(Node, Index), Kind};
            }
          }

        // Remove nothing.
        return {nullptr, RemoveKind::None};
      }

      hash_t ShiftedMask = getMask(Bits) << Shift;
      count_t Index = (Hash & ShiftedMask) >> Shift;
      count_t IndexBit = BitmapType{1u} << Index;

      if (Node->getNodeMap() & IndexBit) {
        count_t Offset = countPopulation(Node->getNodeMap() & (IndexBit - 1));
        auto Result = removeImpl(Node->getInnerChild(Offset), Element, Hash,
                                 Shift + Bits);
        switch (Result.second) {
        case RemoveKind::None:
          // Return the same result, we shouldn't change anything.
          break;
        case RemoveKind::Modified:
          Result.first =
              NodeFactory.replaceInnerNode(Node, Result.first, Offset);
          break;
        case RemoveKind::Trivial:
          if (Node->getNumberOfChildren() != 1 || Shift == 0) {
            // Line of trivial nodes collapsing is over.
            Result.first = NodeFactory.replaceInnerNodeWithData(
                Node, Result.first, IndexBit, Offset);
            Result.second = RemoveKind::Modified;
          } // else remove the same the result because we can
            // easily remove this node.
          break;
        case RemoveKind::Removed:
          if (Node->getNumberOfChildren() == 1) {
            // We need to remove this node as well.
            break;
          }
          if (Node->getNumberOfChildren() == 2) {
            if (Node->getDataMap() != 0 && Shift != 0) {
              // We remove the only inner child, while leaving one data child.
              // This is a trivial situation.
              Result.first = Node->getDataChild(0);
              Result.second = RemoveKind::Trivial;
              break;
            }
          }

          Result.first = NodeFactory.removeInnerNode(Node, IndexBit, Offset);
          Result.second = RemoveKind::Modified;
          break;
        }

        return Result;
      }

      if (Node->getDataMap() & IndexBit) {
        count_t Offset = countPopulation(Node->getDataMap() & (IndexBit - 1));
        const_value_type_ref StoredValue = Node->getData(Offset);
        if (ValueInfo::areEqual(StoredValue, Element)) {
          switch (Node->getNumberOfChildren()) {
          case 1:
            // That is the only child, so we can remove both this child
            // and the inner node containing it.
            return {nullptr, RemoveKind::Removed};
          case 2:
            if (Node->getNodeMap() == 0 && Shift != 0) {
              assert(Offset == 0 || Offset == 1);
              count_t OtherOffset = !Offset;
              return {Node->getDataChild(OtherOffset), RemoveKind::Trivial};
            }
            LLVM_FALLTHROUGH;
          default:
            return {NodeFactory.removeDataChild(Node, IndexBit, Offset),
                    RemoveKind::Modified};
          }
        }
      }

      return {nullptr, RemoveKind::None};
    }

    typename Node::Factory NodeFactory;
  };

  class Iterator {
  public:
    using value_type = const_value_type;
    using pointer = value_type *;
    using reference = value_type &;
    using difference_type = count_t;
    using iterator_category = std::forward_iterator_tag;

    struct EndTag {};
    Iterator(const HAMT &Trie) : Iterator(Trie.Root) {}
    Iterator(const HAMT &Trie, EndTag Tag) : Iterator(Trie.Root, Tag) {}

    Iterator &operator++() {
      --Index;
      walkToLeafs();
      return *this;
    }

    Iterator operator++(int) {
      Iterator Copy = *this;
      operator++();
      return Copy;
    }

    reference operator*() const {
      assert(Index > 0);
      assert(Depth > 0);
      count_t RealIndex = Index - 1;
      if (Depth == getMaxDepth(Bits) + 1) {
        return getCurrentNode()->getCollisions()[RealIndex];
      }

      return getCurrentNode()->getAllChildren()[RealIndex]->getCollisions()[0];
    }
    pointer operator->() const { return &operator*(); }

    bool operator==(const Iterator &RHS) const {
      return Depth == RHS.Depth &&
             (Depth == 0 ||
              (Index == RHS.Index && getCurrentNode() == RHS.getCurrentNode()));
    }
    bool operator!=(const Iterator &RHS) const { return !operator==(RHS); }

  private:
    Iterator(const NodePtr &Root) {
      assignTopLayer(Root);
      if (Root) {
        // Make sure we not on root
        stepDown();
        // Walk to data leafs
        walkToLeafs();
      }
    }

    Iterator(const NodePtr &Root, EndTag Tag) { assignTopLayer(Root); }

    void walkToLeafs() {
      // Check if there are any more data points left in the current layer
      while (Depth > 0 && Index <= getCurrentLayer().size()) {
        // We need to walk into other nodes.
        if (getCurrentLayer().empty()) {
          // We need to go up:
          //
          // 1. Decrease the depth
          --Depth;
          // 2. Be sure that we don't traverse any
          //    data nodes here because we already did.
          Index = 0;
          // 3. Remove the node we came from
          popTopFromTheCurrentLayer();
        } else {
          // We need to go down:
          stepDown();
        }
      }
    }

    void stepDown() {
      // 1. Increase the depth
      ++Depth;
      // 2. Get the element in which we descend
      NodePtr Dest = getCurrentNode();
      // 3. Set current layer and data index
      if (Depth <= getMaxDepth(Bits)) {
        getCurrentLayer() = Dest->getInnerChildren();
        Index = Dest->getAllChildren().size();
      } else {
        // It is a data node, it doesn't have inner children, and thus,
        // the layer is empty.
        getCurrentLayer() = Layer();
        Index = Dest->getCollisions().size();
      }
    }

    using Layer = ArrayRef<NodePtr>;

    void assignTopLayer(const NodePtr &Root) { Layers[0] = Layer(&Root, 1); }

    Layer &getCurrentLayer() { return Layers[Depth]; }
    const Layer &getCurrentLayer() const { return Layers[Depth]; }

    Layer &getParentLayer() {
      assert(Depth > 0 && "Couldn't get parent for zero depth");
      assert(!Layers[Depth - 1].empty() && "Parent layer could not be empty");
      return Layers[Depth - 1];
    }
    const Layer &getParentLayer() const {
      assert(Depth > 0 && "Couldn't get parent for zero depth");
      assert(!Layers[Depth - 1].empty() && "Parent layer could not be empty");
      return Layers[Depth - 1];
    }

    void popTopFromTheCurrentLayer() {
      getCurrentLayer() = getCurrentLayer().slice(1);
    }

    NodePtr getCurrentNode() const { return getParentLayer()[0]; }

    std::array<Layer, getMaxDepth(Bits) + 2> Layers;
    count_t Depth = 0;
    count_t Index = 0;
  };

  LLVM_NODISCARD const_value_type *find(key_type_ref Key) const {
    if (Root == nullptr)
      return nullptr;

    NodePtr Node = Root;
    hash_t Hash = ValueInfo::getHash(Key);

    DEBUG(errs() << "Max depth: " << getMaxDepth(Bits) << ", "
                 << "max shift: " << getMaxShift(Bits) << "\n");
    for (count_t i = 0; i < getMaxDepth(Bits); ++i) {
      count_t Index = Hash & getMask(Bits);
      count_t IndexBit = BitmapType{1u} << Index;

      if (Node->getNodeMap() & IndexBit) {
        count_t Offset = countPopulation(Node->getNodeMap() & (IndexBit - 1));
        Node = Node->getInnerChild(Offset);
        Hash = Hash >> Bits;
        continue;
      }

      if (Node->getDataMap() & IndexBit) {
        count_t Offset = countPopulation(Node->getDataMap() & (IndexBit - 1));
        const_value_type_ref StoredValue = Node->getData(Offset);

        // When we didn't reach the maximal depth, collisions are not possible.
        DEBUG(errs() << StoredValue << " vs. " << Key << "\n");
        if (ValueInfo::areEqual(StoredValue, Key))
          return &StoredValue;
      }

      return nullptr;
    }
    for (const_value_type_ref StoredValue : Node->getCollisions())
      if (ValueInfo::areEqual(StoredValue, Key))
        return &StoredValue;

    return nullptr;
  }

  using RootType = NodePtr;
  HAMT(RootType Root) : Root(Root) {}

  NodePtr getRoot() const { return Root; }

  size_t getSize() const {
    size_t Size = 0;
    if (Root != nullptr) {
      for (auto It = Iterator(*this),
                End = Iterator(*this, typename Iterator::EndTag{});
           It != End; ++It) {
        ++Size;
      }
    }
    return Size;
  }

  bool isEmpty() const { return getSize() == 0; }

  bool isEqual(const HAMT &RHS) const {
    return getSize() == RHS.getSize() && areTreesEqual(Root, RHS.Root);
  }

private:
  static bool areTreesEqual(NodePtr LHS, NodePtr RHS, count_t Depth = 0) {
    if (LHS == RHS)
      return true;

    if (Depth == getMaxDepth(Bits)) {
      return areCollisionsEqual(LHS->getCollisions(), RHS->getCollisions());
    }

    if (LHS->getNodeMap() != RHS->getNodeMap() ||
        LHS->getDataMap() != RHS->getDataMap())
      return false;

    return areInnerNodesEqual(LHS, RHS, Depth);
  }

  static bool areInnerNodesEqual(NodePtr LHS, NodePtr RHS, count_t Depth) {
    ArrayRef<NodePtr> LHSInnerNodes = LHS->getInnerChildren();
    ArrayRef<NodePtr> RHSInnerNodes = RHS->getInnerChildren();

    if (LHSInnerNodes.size() != RHSInnerNodes.size())
      return false;

    for (std::tuple<NodePtr, NodePtr> NodesToCompare :
         zip(LHSInnerNodes, RHSInnerNodes)) {
      if (!areTreesEqual(std::get<0>(NodesToCompare),
                         std::get<1>(NodesToCompare), Depth + 1))
        return false;
    }

    ArrayRef<NodePtr> LHSDataNodes =
        LHS->getAllChildren().slice(LHSInnerNodes.size());
    ArrayRef<NodePtr> RHSDataNodes =
        RHS->getAllChildren().slice(RHSInnerNodes.size());

    return std::equal(LHSDataNodes.begin(), LHSDataNodes.end(),
                      RHSDataNodes.begin(), [](NodePtr LHS, NodePtr RHS) {
                        return ValueInfo::areEqual(LHS->getCollisions()[0],
                                                   RHS->getCollisions()[0]);
                      });
  }

  static bool areCollisionsEqual(ArrayRef<value_type> LHS,
                                 ArrayRef<value_type> RHS) {
    return llvm::all_of(LHS, [RHS](const_value_type_ref Needle) {
      return llvm::find_if(RHS, [Needle](const_value_type_ref Candidate) {
        return ValueInfo::areEqual(Needle, Candidate);
      });
    });
  }
};

} // end namespace detail

template <class T> struct ImmutableHashSetInfo {
  using value_type = T;
  using const_value_type = const T;
  using value_type_ref = T &;
  using const_value_type_ref = const T &;
  using key_type = const T;
  using key_type_ref = const T &;
  using ProfileInfo = ImutProfileInfo<T>;

  static void Profile(FoldingSetNodeID ID, const_value_type_ref Value) {
    ProfileInfo::Profile(ID, Value);
  }

  static detail::hash_t getHash(key_type_ref Key) {
    FoldingSetNodeID ID;
    ProfileInfo::Profile(ID, Key);
    return ID.ComputeHash();
  }
  static bool areEqual(const_value_type_ref LHS, const_value_type_ref RHS) {
    return LHS == RHS;
  }
};

template <class T, class ValueInfo = ImmutableHashSetInfo<T>>
class ImmutableHashSet {
public:
  using value_type = typename ValueInfo::value_type;
  using const_value_type = typename ValueInfo::const_value_type;
  using value_type_ref = typename ValueInfo::value_type_ref;
  using const_value_type_ref = typename ValueInfo::const_value_type_ref;

private:
  using Trie = detail::HAMT<ValueInfo>;
  Trie Impl;
  /* implicit */ ImmutableHashSet(Trie From) : Impl(From) {}

public:
  class Factory {
  public:
    Factory() = default;
    Factory(BumpPtrAllocator &Alloc) : Impl(Alloc) {}

    LLVM_NODISCARD ImmutableHashSet getEmptySet() { return Impl.getEmptySet(); }
    LLVM_NODISCARD ImmutableHashSet add(ImmutableHashSet Original,
                                        const_value_type_ref Value) {
      return Impl.add(Original.Impl, Value);
    }
    LLVM_NODISCARD ImmutableHashSet remove(ImmutableHashSet Original,
                                           const_value_type_ref Value) {
      return Impl.remove(Original.Impl, Value);
    }

  private:
    typename Trie::Factory Impl;
  };

  using iterator = typename Trie::Iterator;
  iterator begin() const { return iterator(Impl); }
  iterator end() const { return iterator(Impl, typename iterator::EndTag{}); }

  bool contains(const_value_type_ref Value) const { return Impl.find(Value); }
  size_t getSize() const { return Impl.getSize(); }
  bool isEmpty() const { return Impl.isEmpty(); }

  using RootType = typename Trie::RootType;
  RootType getRoot() const { return Impl.getRoot(); }

  ImmutableHashSet(RootType Root) : Impl(Root) {}

  bool operator==(const ImmutableHashSet &RHS) const {
    return Impl.isEqual(RHS.Impl);
  }

  bool operator!=(const ImmutableHashSet &RHS) const {
    return !operator==(RHS);
  }

  static void Profile(FoldingSetNodeID &ID, const ImmutableHashSet &S) {
    ID.AddPointer(S.getRoot());
  }

  void Profile(FoldingSetNodeID &ID) const { return Profile(ID, *this); }
};

} // end namespace llvm

#endif // LLVM_ADT_IMMUTABLEHASHSET_H
