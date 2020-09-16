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

#define D(X)

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
    };

    struct DataNode final : public TrailingObjects<DataNode, value_type> {
      size_t Size;
    };

    union ImplType {
      InnerNode Inner;
      DataNode Data;
    };

    template <class NodeType> struct NodeTraitImpl;

    template <> struct NodeTraitImpl<InnerNode> {
      using TrailingType = NodePtr;
    };

    template <> struct NodeTraitImpl<DataNode> {
      using TrailingType = value_type;
    };

    template <class NodeType>
    using NodeTrait = typename NodeTraitImpl<NodeType>::TrailingType;

    ImplType Impl;

  public:
    class Factory {
    public:
      Node *addCollision(NodePtr Original, const_value_type_ref NewElement) {
        D(llvm::errs() << "addCollision(NodePtr Original, const_value_type_ref "
                          "NewElement)\n");
        DataNode &DesugaredOriginal = Original->Impl.Data;
        const size_t NewSize = DesugaredOriginal.Size + 1;
        Node *NewNode = allocate<DataNode>(NewSize);

        DataNode &DesugaredNew = NewNode->Impl.Data;
        DesugaredNew.Size = NewSize;
        llvm::copy(Original->getCollisions(), NewNode->getCollisions().begin());
        NewNode->getCollisions()[NewSize - 1] = NewElement;

        D(llvm::errs() << "New size: " << NewSize << "\n");

        return NewNode;
      }

      Node *replaceCollision(NodePtr Original, const_value_type_ref NewElement,
                             count_t Index) {
        D(llvm::errs() << "replaceCollision(NodePtr Original, "
                          "const_value_type_ref NewElement, count_t Index)\n");
        DataNode &DesugaredOriginal = Original->Impl.Data;
        Node *NewNode = allocate<DataNode>(DesugaredOriginal.Size);

        DataNode &DesugaredNew = NewNode->Impl.Data;
        DesugaredNew.Size = DesugaredOriginal.Size;
        llvm::copy(Original->getCollisions(), NewNode->getCollisions().begin());
        NewNode->getCollisions()[Index] = NewElement;

        return NewNode;
      }

      Node *replaceInnerNode(NodePtr Original, NodePtr NewChild,
                             count_t Offset) {
        D(llvm::errs()
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
        D(llvm::errs() << "replaceDataNode(NodePtr Original, "
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
        D(llvm::errs()
          << "mergeValues(shift_t Shift, const_value_type_ref First, hash_t "
             "FirstHash, const_value_type_ref Second, hash_t SecondHash)\n");
        D(errs() << "Merging " << First << " and " << Second << "\n");
        if (LLVM_LIKELY(Shift < getMaxShift(Bits))) {
          hash_t ShiftedMask = (hash_t)getMask(Bits) << Shift;
          D(errs() << "Population " << countPopulation(ShiftedMask)
                   << ", shift " << Shift << "\n");
          hash_t FirstIndex = FirstHash & ShiftedMask;
          hash_t SecondIndex = SecondHash & ShiftedMask;
          D(errs() << "First " << (FirstIndex >> Shift) << ", Second "
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
        D(llvm::errs() << "createDataNode(const_value_type_ref Data)\n");
        constexpr size_t NewSize = 1;
        Node *NewNode = allocate<DataNode>(NewSize);

        DataNode &DesugaredNew = NewNode->Impl.Data;
        DesugaredNew.Size = NewSize;
        NewNode->getCollisions()[0] = Data;

        return NewNode;
      }
      Node *createDataNode(const_value_type_ref First,
                           const_value_type_ref Second) {
        D(llvm::errs() << "createDataNode(const_value_type_ref First, "
                          "const_value_type_ref Second)\n");
        constexpr size_t NewSize = 2;
        Node *NewNode = allocate<DataNode>(NewSize);

        DataNode &DesugaredNew = NewNode->Impl.Data;
        DesugaredNew.Size = NewSize;
        NewNode->getCollisions()[0] = First;
        NewNode->getCollisions()[1] = Second;

        return NewNode;
      }
      Node *createInnerNode(count_t Index, const_value_type_ref Element) {
        D(llvm::errs()
          << "createInnerNode(count_t Index, const_value_type_ref Element)\n");
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
        D(llvm::errs() << "createInnerNode(count_t Index, NodePtr Child)\n");
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
        D(llvm::errs()
          << "createInnerNode(count_t FirstIndex, const_value_type_ref "
             "First, count_t SecondIndex, const_value_type_ref Second)\n");
        D(errs() << "Indices: " << FirstIndex << ", " << SecondIndex << "\n");
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

      Node *replaceDataNodeWithInner(NodePtr Original, NodePtr NewChild,
                                     count_t IndexBit, count_t DataOffset) {
        D(llvm::errs() << "replaceDataNodeWithInner(NodePtr Original, NodePtr "
                          "NewChild, count_t IndexBit, count_t DataOffset)\n");

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
        D(llvm::errs() << "addDataChild(NodePtr Original, count_t IndexBit, "
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

      template <class NodeType> Node *allocate(size_t Size) {
        // TODO: calculate correct size
        void *Buffer = Arena.Allocate(
            sizeof(Node) +
                NodeType::template additionalSizeToAlloc<NodeTrait<NodeType>>(
                    Size),
            alignof(Node));
        Node *Result = new (Buffer) Node();
        new (&Result->Impl) NodeType();
        return Result;
      }

      BumpPtrAllocator Arena;
    };

    BitmapType getNodeMap() const { return Impl.Inner.NodeMap; }
    BitmapType getDataMap() const { return Impl.Inner.DataMap; }
    MutableArrayRef<NodePtr> getAllChildren() {
      return {Impl.Inner.template getTrailingObjects<NodePtr>(),
              Impl.Inner.Size};
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
      return Impl.Inner.Size - 1 - Offset;
    }

    MutableArrayRef<value_type> getCollisions() {
      return {Impl.Data.template getTrailingObjects<value_type>(),
              Impl.Data.Size};
    }
  };

  HAMT(NodePtr Root, size_t Size) : Root(Root), Size(Size) {}

  NodePtr Root;
  size_t Size;

public:
  class Factory {
  public:
    LLVM_NODISCARD HAMT getEmptySet() { return {nullptr, 0}; }
    LLVM_NODISCARD HAMT add(HAMT Trie, const_value_type_ref Element) {
      hash_t Hash = ValueInfo::getHash(Element);
      auto Result = addImpl(Trie.Root, Element, Hash);
      size_t NewSize = Trie.getSize() + static_cast<size_t>(Result.second);
      return {Result.first, NewSize};
    }
    LLVM_NODISCARD HAMT remove(const_value_type_ref Element) {
      return getEmptySet();
    }

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

    typename Node::Factory NodeFactory;
  };

  LLVM_NODISCARD const_value_type *find(key_type_ref Key) const {
    NodePtr Node = Root;
    hash_t Hash = ValueInfo::getHash(Key);

    D(errs() << "Max depth: " << getMaxDepth(Bits) << ", "
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
        D(errs() << StoredValue << " vs. " << Key << "\n");
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

  size_t getSize() const { return Size; }

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

    for (std::pair<NodePtr, NodePtr> NodesToCompare :
         zip(LHSInnerNodes, RHSInnerNodes)) {
      if (!areTreesEqual(NodesToCompare.first, NodesToCompare.second,
                         Depth + 1))
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

template <class T> struct ImmutableHasSetInfo {
  using value_type = T;
  using const_value_type = const T;
  using value_type_ref = T &;
  using const_value_type_ref = const T &;
  using key_type = const T;
  using key_type_ref = const T &;

  static detail::hash_t getHash(key_type_ref Key) {
    return llvm::hash_value(Key);
  }
  static bool areEqual(const_value_type_ref LHS, const_value_type_ref RHS) {
    return LHS == RHS;
  }
};

template <class T, class ValueInfo = ImmutableHasSetInfo<T>>
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
    LLVM_NODISCARD ImmutableHashSet getEmptySet() { return Impl.getEmptySet(); }
    LLVM_NODISCARD ImmutableHashSet add(ImmutableHashSet Original,
                                        const_value_type_ref Value) {
      return Impl.add(Original.Impl, Value);
    }

  private:
    typename Trie::Factory Impl;
  };

  bool contains(const_value_type_ref Value) const { return Impl.find(Value); }
  size_t getSize() const { return Impl.getSize(); }

  bool operator==(const ImmutableHashSet &RHS) const {
    return Impl.isEqual(RHS.Impl);
  }

  bool operator!=(const ImmutableHashSet &RHS) const {
    return !operator==(RHS);
  }
};

} // end namespace llvm

#endif // LLVM_ADT_IMMUTABLEHASHSET_H
