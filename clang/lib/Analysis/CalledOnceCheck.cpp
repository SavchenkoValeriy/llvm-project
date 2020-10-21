#include "clang/Analysis/Analyses/CalledOnceCheck.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/StmtVisitor.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/FlowSensitive/DataflowWorklist.h"
#include "clang/Basic/LLVM.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"

using namespace clang;

namespace {
static constexpr unsigned EXPECTED_MAX_NUMBER_OF_PARAMS = 2;
template <class T>
using ParamSizedVector = llvm::SmallVector<T, EXPECTED_MAX_NUMBER_OF_PARAMS>;
static constexpr unsigned EXPECTED_NUMBER_OF_BASIC_BLOCKS = 8;
template <class T>
using CFGSizedVector = llvm::SmallVector<T, EXPECTED_NUMBER_OF_BASIC_BLOCKS>;

class ParameterStatus {
public:
  enum Kind {
    // Escaped marks situations when marked parameter escaped into
    // another function (so we can assume that it was possibly called there).
    Escaped = 0x0, /* 000 */
    // Parameter was not yet called.
    NotCalled = 0x1, /* 001 */
    // Parameter was definitely called once at this point.
    DefinitelyCalled = 0x2, /* 010 */
    // Parameter was not called at least on one path leading to this point,
    // while there is also at least one path that it gets called.
    MaybeCalled = 0x3, /* 011 */
    // Parameter was not yet analyzed.
    NotVisited = 0x4, /* 100 */
    // We already reported a violation and stopped tracking calls for this
    // parameter.
    Reported = 0x7, /* 111 */
    LLVM_MARK_AS_BITMASK_ENUM(/* LargestValue = */ Reported)
  };

  constexpr ParameterStatus() = default;
  /* implicit */ ParameterStatus(Kind K) : StatusKind(K) {
    assert(!canBeCalled(K) && "Can't initialize status without a call");
  }
  ParameterStatus(Kind K, const Expr *Call) : StatusKind(K), Call(Call) {
    assert(canBeCalled(K) && "This kind is not supposed to have a call");
  }

  const Expr &getCall() const {
    assert(canBeCalled(getKind()) && "ParameterStatus doesn't have a call");
    return *Call;
  }
  static bool canBeCalled(Kind K) {
    return K & DefinitelyCalled && K != Reported;
  }
  bool canBeCalled() const { return canBeCalled(getKind()); }
  Kind getKind() const { return StatusKind; }

  void join(const ParameterStatus &Other) {
    // If we have a pointer already, let's keep it.
    // For the purposes of the analysis, it doesn't really matter
    // which call we report.
    //
    // If we don't have a pointer, let's take whatever gets joined.
    if (!Call) {
      Call = Other.Call;
    }
    // Join kinds.
    StatusKind |= Other.getKind();
  }

  bool operator==(const ParameterStatus &Other) const {
    // We compare only kinds, pointers on their own is only additional
    // information.
    return getKind() == Other.getKind();
  }

private:
  // It would've been a perfect place to use llvm::PointerIntPair, but
  // unfortunately NumLowBitsAvailable for clang::Expr had been reduced to 2.
  Kind StatusKind = NotVisited;
  const Expr *Call = nullptr;
};

class State {
public:
  State(unsigned Size, ParameterStatus::Kind K = ParameterStatus::NotVisited)
      : ParamData(Size, K) {}

  ParameterStatus &getStatusFor(unsigned Index) { return ParamData[Index]; }
  const ParameterStatus &getStatusFor(unsigned Index) const {
    return ParamData[Index];
  }

  /// Return true if parameter with the given index can be called.
  bool canBeCalled(unsigned Index) const {
    return getStatusFor(Index).canBeCalled();
  }
  /// Return a reference that we consider a call.
  ///
  /// Should only be used for parameters that can be called.
  const Expr &getCallFor(unsigned Index) const {
    return getStatusFor(Index).getCall();
  }
  /// Return status kind of parameter with the given index.
  ParameterStatus::Kind getKindFor(unsigned Index) const {
    return getStatusFor(Index).getKind();
  }

  bool isVisited() const {
    return llvm::all_of(ParamData, [](const ParameterStatus &S) {
      return S.getKind() != ParameterStatus::NotVisited;
    });
  }

  void join(const State &Other) {
    assert(ParamData.size() == Other.ParamData.size() &&
           "Couldn't join statuses with different sizes");
    for (auto Pair : llvm::zip(ParamData, Other.ParamData)) {
      std::get<0>(Pair).join(std::get<1>(Pair));
    }
  }

  using iterator = ParamSizedVector<ParameterStatus>::iterator;
  using const_iterator = ParamSizedVector<ParameterStatus>::const_iterator;

  iterator begin() { return ParamData.begin(); }
  iterator end() { return ParamData.end(); }

  const_iterator begin() const { return ParamData.begin(); }
  const_iterator end() const { return ParamData.end(); }

  bool operator==(const State &Other) const {
    return ParamData == Other.ParamData;
  }

private:
  ParamSizedVector<ParameterStatus> ParamData;
};

bool shouldBeCalledOnce(const ParmVarDecl *Parameter) {
  // TODO: check for conventions
  return Parameter->hasAttr<CalledOnceAttr>();
}

bool shouldBeCalledOnce(const CallExpr *Call, unsigned ParamIndex) {
  const FunctionDecl *Function = Call->getDirectCallee();
  return Function && ParamIndex < Function->getNumParams() &&
         shouldBeCalledOnce(Function->getParamDecl(ParamIndex));
}

template <class IteratorRange>
const Stmt *getFirstStmtFromRange(IteratorRange Range) {
  for (const CFGElement &Element : Range) {
    if (Optional<CFGStmt> S = Element.getAs<CFGStmt>()) {
      return S->getStmt();
    }
  }
  return nullptr;
}

LLVM_ATTRIBUTE_UNUSED const Stmt *getFirstStmt(const CFGBlock *BB) {
  return getFirstStmtFromRange(*BB);
}

LLVM_ATTRIBUTE_UNUSED const Stmt *getLastStmt(const CFGBlock *BB) {
  return getFirstStmtFromRange(llvm::reverse(*BB));
}

class NotCalledClarifier
    : public ConstStmtVisitor<NotCalledClarifier, void, const CFGBlock *,
                              const CFGBlock *, const ParmVarDecl *> {
public:
  NotCalledClarifier(CalledOnceCheckHandler &Handler) : Handler(Handler) {}

  void reportMaybeNotCalled(const CFGBlock *Conditional,
                            const CFGBlock *SuccWithoutCall,
                            const ParmVarDecl *Parameter) {
    if (const Stmt *Terminator = Conditional->getTerminatorStmt()) {
      Visit(Terminator, Conditional, SuccWithoutCall, Parameter);
    }
  }

  void VisitIfStmt(const IfStmt *If, const CFGBlock *Conditional,
                   const CFGBlock *SuccWithoutCall,
                   const ParmVarDecl *Parameter) {
    NeverCalledReason Branch = NeverCalledReason::IfElse;

    if (doesBlockRepresent(SuccWithoutCall, If->getThen())) {
      Branch = NeverCalledReason::IfThen;
    }

    Handler.handleNeverCalled(Parameter, If, Branch);
  }

  void VisitAbstractConditionalOperator(
      const AbstractConditionalOperator *Ternary, const CFGBlock *Conditional,
      const CFGBlock *SuccWithoutCall, const ParmVarDecl *Parameter) {
    NeverCalledReason Branch = NeverCalledReason::IfElse;

    Ternary->getTrueExpr()->dump();
    getFirstStmt(SuccWithoutCall)->dump();
    if (doesBlockRepresent(SuccWithoutCall, Ternary->getTrueExpr())) {
      Branch = NeverCalledReason::IfThen;
    }

    Handler.handleNeverCalled(Parameter, Ternary, Branch);
  }

  void VisitSwitchStmt(const SwitchStmt *Switch, const CFGBlock *Conditional,
                       const CFGBlock *SuccWithoutCall,
                       const ParmVarDecl *Parameter) {
    const Stmt *CaseToBlame = SuccWithoutCall->getLabel();
    if (!CaseToBlame) {
      // If interesting basic block is not labeled, it means that this
      // basic block does not represent any of the cases.
      Handler.handleNeverCalled(Parameter, Switch,
                                NeverCalledReason::SwitchSkipped);
      return;
    }

    for (const SwitchCase *Case = Switch->getSwitchCaseList(); Case;
         Case = Case->getNextSwitchCase()) {
      if (Case == CaseToBlame) {
        Handler.handleNeverCalled(Parameter, Case, NeverCalledReason::Switch);
      }
    }
  }

  void VisitForStmt(const ForStmt *For, const CFGBlock *Conditional,
                    const CFGBlock *SuccWithoutCall,
                    const ParmVarDecl *Parameter) {
    NeverCalledReason Branch = NeverCalledReason::LoopSkipped;

    if (doesBlockRepresent(SuccWithoutCall, For->getBody())) {
      Branch = NeverCalledReason::LoopEntered;
    }

    Handler.handleNeverCalled(Parameter, For, Branch);
  }

  void VisitWhileStmt(const WhileStmt *While, const CFGBlock *Conditional,
                      const CFGBlock *SuccWithoutCall,
                      const ParmVarDecl *Parameter) {
    NeverCalledReason Branch = NeverCalledReason::LoopSkipped;

    if (doesBlockRepresent(SuccWithoutCall, While->getBody())) {
      Branch = NeverCalledReason::LoopEntered;
    }

    Handler.handleNeverCalled(Parameter, While, Branch);
  }

  static bool doesBlockRepresent(const CFGBlock *Block, const Stmt *What) {
    const Stmt *UnwrappedStmt = unwrapCompoundStmt(What);
    if (UnwrappedStmt) {
      for (const CFGElement &Element : *Block) {
        if (Optional<CFGStmt> S = Element.getAs<CFGStmt>())
          if (UnwrappedStmt == S->getStmt())
            return true;
      }
      return false;
    }

    return (Block->empty());
  }

  static const Stmt *unwrapCompoundStmt(const Stmt *PossiblyCompound) {
    if (const auto *Compound =
            dyn_cast_or_null<CompoundStmt>(PossiblyCompound)) {
      // NOTE: this can be also nullptr for empty compound statements
      return Compound->body_front();
    }
    // This case covers both when PossiblyCompound is not a compund statement
    // and when it is a null pointer.
    return PossiblyCompound;
  }

private:
  CalledOnceCheckHandler &Handler;
};

class CalledOnceChecker : public ConstStmtVisitor<CalledOnceChecker> {
public:
  static void check(const CFG &FunctionCFG, AnalysisDeclContext &AC,
                    CalledOnceCheckHandler &Handler) {
    CalledOnceChecker(FunctionCFG, AC, Handler).check();
  }

private:
  CalledOnceChecker(const CFG &FunctionCFG, AnalysisDeclContext &AC,
                    CalledOnceCheckHandler &Handler)
      : FunctionCFG(FunctionCFG), AC(AC), Handler(Handler), CurrentState(0) {
    initDataStructures();
    assert(size() == 0 || !States.empty());
  }

  void initDataStructures() {
    const Decl *AnalyzedDecl = AC.getDecl();
    // TODO: check different types of functions to check
    if (const auto *Function = dyn_cast<FunctionDecl>(AnalyzedDecl)) {
      findParamsToTrack(Function);
    } else if (const auto *Block = dyn_cast<BlockDecl>(AnalyzedDecl)) {
      findCapturesToTrack(Block);
      findParamsToTrack(Block);
    }

    // Have something to track, let's init states for every block from the CFG.
    if (size() != 0) {
      States =
          CFGSizedVector<State>(FunctionCFG.getNumBlockIDs(), State(size()));
    }
  }

  void findCapturesToTrack(const BlockDecl *Block) {
    for (const auto &Capture : Block->captures()) {
      if (const auto *P = dyn_cast<ParmVarDecl>(Capture.getVariable())) {
        if (shouldBeCalledOnce(P)) {
          TrackedParams.push_back(P);
        }
      }
    }
  }

  template <class FunctionLikeDecl>
  void findParamsToTrack(const FunctionLikeDecl *Function) {
    for (const ParmVarDecl *P : Function->parameters()) {
      if (shouldBeCalledOnce(P)) {
        TrackedParams.push_back(P);
      }
    }
  }

  void check() {
    // Nothing to check here: we don't have marked parameters.
    if (size() == 0)
      return;

    assert(
        llvm::none_of(States, [](const State &S) { return S.isVisited(); }) &&
        "None of the blocks should be 'visited' before the analysis");

    // FunctionCFG.dump(AC.getASTContext().getLangOpts(), false);

    BackwardDataflowWorklist Worklist(FunctionCFG, AC);

    const CFGBlock *Exit = &FunctionCFG.getExit();
    assignState(Exit, State(size(), ParameterStatus::NotCalled));
    Worklist.enqueuePredecessors(Exit);

    while (const CFGBlock *BB = Worklist.dequeue()) {
      assert(BB && "Worklist should filter out null blocks");
      check(BB);
      assert(CurrentState.isVisited() &&
             "After the check, basic block should be visited");

      // Traverse successor basic blocks if the status of this block
      // has changed.
      if (assignState(BB, CurrentState)) {
        Worklist.enqueuePredecessors(BB);
      }
    }

    checkEntry(&FunctionCFG.getEntry());
  }

  void check(const CFGBlock *BB) {
    CurrentState = joinSuccessors(BB);

    for (const CFGElement &Element : llvm::reverse(*BB)) {
      if (Optional<CFGStmt> S = Element.getAs<CFGStmt>()) {
        check(S->getStmt());
      }
    }
  }
  void check(const Stmt *S) { Visit(S); }

  void checkEntry(const CFGBlock *Entry) {
    const State &EntryStatus = getState(Entry);
    llvm::BitVector Interesting(size(), false);

    // Check if there are no calls of the marked parameter at all
    for (const auto &IndexedStatus : llvm::enumerate(EntryStatus)) {
      const ParmVarDecl *Parameter = getParameter(IndexedStatus.index());
      switch (IndexedStatus.value().getKind()) {
      case ParameterStatus::NotCalled:
        if (isCaptured(Parameter)) {
          Handler.handleCapturedNeverCalled(Parameter, AC.getDecl());
        } else {
          Handler.handleNeverCalled(Parameter);
        }

        break;
      case ParameterStatus::MaybeCalled:
        // If we have 'maybe called' at this point, we have an error
        // that there is at least one path where this parameter
        // is not called.
        //
        // However, reporting the warning with only that information can be
        // too vague for the users.  For this reason, we mark such parameters
        // as "interesting" for further analysis.
        Interesting[IndexedStatus.index()] = true;
        break;
      default:
        break;
      }
    }

    // Early exit if we don't have parameters for extra analysis
    if (Interesting.none())
      return;

    NotCalledClarifier Clarifier(Handler);

    // We are looking for a pair of blocks A, B so that the following is true:
    //   * A is marked as MaybeCalled
    //   * B is marked as NotCalled
    //   * A is a predecessor of B
    //
    // In that situation, it is guaranteed that A has at least one successor
    // other than B and that B is the first block of the path where the user
    // doesn't call parameter in question.
    //
    // For this reason, branch A -> B can be used for reporting.
    for (const CFGBlock *BB : FunctionCFG) {
      const State &BlockState = getState(BB);
      for (unsigned Index : llvm::seq(0u, size())) {
        if (!Interesting[Index])
          continue;

        if (BlockState.getKindFor(Index) == ParameterStatus::MaybeCalled) {
          for (const CFGBlock *Succ : BB->succs()) {
            if (!Succ)
              continue;

            if (getState(Succ).getKindFor(Index) ==
                ParameterStatus::NotCalled) {
              assert(BB->succ_size() >= 2 &&
                     "Block should have at least two successors at this point");
              Clarifier.reportMaybeNotCalled(BB, Succ, getParameter(Index));
            }
          }
        }
      }
    }
  }

  void checkDirectCall(const CallExpr *Call) {
    if (auto Index = getIndexOfCallee(Call)) {
      processCallFor(*Index, Call);
    }
  }

  /// Check the call expression for being an indirect call of one of the tracked
  /// parameters.  It is indirect in the sense that this particular call is not
  /// calling the parameter itself, but rather uses it as the argument.
  void checkIndirectCall(const CallExpr *Call) {
    // CallExpr::arguments does not interact nicely with llvm::enumerate.
    llvm::ArrayRef<const Expr *> Arguments =
        llvm::makeArrayRef(Call->getArgs(), Call->getNumArgs());

    // Let's check if any of the call arguments is a point of interest.
    for (const auto &Argument : llvm::enumerate(Arguments)) {
      if (auto Index = getIndexOfExpression(Argument.value())) {
        ParameterStatus &CurrentParamStatus = CurrentState.getStatusFor(*Index);

        if (shouldBeCalledOnce(Call, Argument.index())) {
          // If the corresponding parameter is marked as 'called_once' we should
          // consider it as a call.
          processCallFor(*Index, Call);
        } else if (CurrentParamStatus.getKind() == ParameterStatus::NotCalled) {
          // Otherwise, we mark this parameter as escaped, which can be
          // interpreted both as called or not called depending on the context.
          CurrentParamStatus = ParameterStatus::Escaped;
        }
        // Otherwise, let's keep the state as it is.
      }
    }
  }

  /// Process call of the parameter with the given index
  void processCallFor(unsigned Index, const Expr *Call) {
    ParameterStatus &CurrentParamStatus = CurrentState.getStatusFor(Index);

    if (CurrentParamStatus.canBeCalled()) {

      // At this point, this parameter was called, so this is a second call.
      Handler.handleDoubleCall(getParameter(Index),
                               &CurrentState.getCallFor(Index), Call);
      // Mark this parameter as already reported on, so we don't repeat
      // warnings.
      CurrentParamStatus = ParameterStatus::Reported;

    } else if (CurrentParamStatus.getKind() != ParameterStatus::Reported) {
      // If we didn't report anything yet, let's mark this parameter
      // as called.
      ParameterStatus Called(ParameterStatus::DefinitelyCalled, Call);
      CurrentParamStatus = Called;
    }
  }

  bool isCaptured(const ParmVarDecl *Parameter) const {
    if (const BlockDecl *Block = dyn_cast<BlockDecl>(AC.getDecl())) {
      return Block->capturesVariable(Parameter);
    }
    return false;
  }

  /// Return status stored for the given basic block.
  State &getState(const CFGBlock *BB) {
    assert(BB);
    return States[BB->getBlockID()];
  }
  const State &getState(const CFGBlock *BB) const {
    assert(BB);
    return States[BB->getBlockID()];
  }

  /// Assign status to the given basic block.
  ///
  /// Returns true when the stored status changed.
  bool assignState(const CFGBlock *BB, const State &ToAssign) {
    State &Current = getState(BB);
    if (Current == ToAssign) {
      return false;
    }

    Current = ToAssign;
    return true;
  }

  /// Join all incoming statuses for the given basic block.
  State joinSuccessors(const CFGBlock *BB) const {
    assert(!BB->succ_empty() && "Basic block should have successors to join");
    State Result = getState(*BB->succ_begin());

    for (const CFGBlock *Succ : llvm::drop_begin(BB->succs(), 1)) {
      if (!Succ)
        continue;

      Result.join(getState(Succ));
    }

    return Result;
  }

public:
  // Tree traversal methods
  void VisitCallExpr(const CallExpr *Call) {
    checkDirectCall(Call);
    checkIndirectCall(Call);
  }

  void VisitBlockExpr(const BlockExpr *Block) {
    for (const auto &Capture : Block->getBlockDecl()->captures()) {
      if (const auto *P = dyn_cast<ParmVarDecl>(Capture.getVariable())) {
        if (auto Index = getIndex(*P)) {
          processCallFor(*Index, Block);
        }
      }
    }
  }

private:
  unsigned size() const { return TrackedParams.size(); }

  llvm::Optional<unsigned> getIndexOfCallee(const CallExpr *Call) const {
    return getIndexOfExpression(Call->getCallee());
  }

  llvm::Optional<unsigned> getIndexOfExpression(const Expr *E) const {
    return getIndexOfReferencedParameter(
        dyn_cast<DeclRefExpr>(E->IgnoreCasts()));
  }

  llvm::Optional<unsigned>
  getIndexOfReferencedParameter(const DeclRefExpr *DR) const {
    if (DR) {
      if (const auto *Parameter = dyn_cast<ParmVarDecl>(DR->getDecl())) {
        return getIndex(*Parameter);
      }
    }

    return llvm::None;
  }
  llvm::Optional<unsigned> getIndex(const ParmVarDecl &Parameter) const {
    ParamSizedVector<const ParmVarDecl *>::const_iterator It =
        llvm::find(TrackedParams, &Parameter);

    if (It != TrackedParams.end()) {
      return It - TrackedParams.begin();
    }

    return llvm::None;
  }
  const ParmVarDecl *getParameter(unsigned Index) const {
    assert(Index < TrackedParams.size());
    return TrackedParams[Index];
  }

  const CFG &FunctionCFG;
  AnalysisDeclContext &AC;
  CalledOnceCheckHandler &Handler;

  State CurrentState;
  ParamSizedVector<const ParmVarDecl *> TrackedParams;
  CFGSizedVector<State> States;
};

} // end anonymous namespace

namespace clang {
void checkCalledOnceParameters(const CFG &FunctionCFG, AnalysisDeclContext &AC,
                               CalledOnceCheckHandler &Handler) {
  CalledOnceChecker::check(FunctionCFG, AC, Handler);
}
} // end namespace clang
