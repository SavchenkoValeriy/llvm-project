#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_CALLEDONCECHECK_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_CALLEDONCECHECK_H

namespace clang {

class AnalysisDeclContext;
class CFG;
class Decl;
class DeclContext;
class Expr;
class ParmVarDecl;
class Stmt;

enum class NeverCalledReason {
  IfThen,
  IfElse,
  Switch,
  SwitchSkipped,
  LoopEntered,
  LoopSkipped
};

class CalledOnceCheckHandler {
public:
  CalledOnceCheckHandler() = default;
  virtual ~CalledOnceCheckHandler() = default;

  virtual void handleDoubleCall(const ParmVarDecl *Parameter, const Expr *Call,
                                const Expr *PrevCall) {}
  virtual void handleNeverCalled(const ParmVarDecl *Parameter) {}
  virtual void handleNeverCalled(const ParmVarDecl *Parameter,
                                 const Stmt *Where, NeverCalledReason Reason) {}
  virtual void handleCapturedNeverCalled(const ParmVarDecl *Parameter,
                                         const Decl *Where) {}
};

void checkCalledOnceParameters(const CFG &, AnalysisDeclContext &,
                               CalledOnceCheckHandler &);

} // end namespace clang

#endif /* LLVM_CLANG_ANALYSIS_ANALYSES_CALLEDONCECHECK_H */
