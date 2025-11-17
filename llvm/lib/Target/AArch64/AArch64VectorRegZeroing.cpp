//===- AArch64VectorRegZeroing.cpp - Zero unused vector regs before loops ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This pass zeros out unused NEON/FP vector registers before entering loops
/// to increase the number of physical registers available for renaming.
///
/// Background:
/// On Apple Silicon (and other AArch64 cores), explicitly zeroing unused
/// vector registers can improve performance by freeing up physical registers
/// in the register file. When a vector register is written with zero, the
/// microarchitecture can reclaim the associated physical register and add it
/// to the free list for renaming.
///
/// This is particularly beneficial for:
/// - Loops with high FP/NEON instruction density
/// - Code that exhausts the physical register file
/// - E-cores with smaller physical register files
///
/// The optimization zeros caller-saved vector registers (Q0-Q7, Q16-Q31) that
/// are not used within a loop, inserting the zeroing instructions in the loop
/// preheader.
///
/// Reference: Apple Silicon Performance Tuning Guide, Section 4.6.3
//===----------------------------------------------------------------------===//

#include "AArch64.h"
#include "AArch64InstrInfo.h"
#include "AArch64Subtarget.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/InitializePasses.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "aarch64-vector-reg-zeroing"

STATISTIC(NumLoopsProcessed, "Number of loops processed");
STATISTIC(NumLoopsSkipped, "Number of loops skipped (no preheader)");
STATISTIC(NumRegistersZeroed, "Number of vector registers zeroed");

namespace {

class AArch64VectorRegZeroing : public MachineFunctionPass {
public:
  static char ID;

  AArch64VectorRegZeroing() : MachineFunctionPass(ID) {}

  bool runOnMachineFunction(MachineFunction &MF) override;

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineLoopInfoWrapperPass>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  MachineFunctionProperties getRequiredProperties() const override {
    return MachineFunctionProperties().setNoVRegs();
  }

  StringRef getPassName() const override {
    return "AArch64 Vector Register Zeroing";
  }

private:
  const AArch64InstrInfo *TII = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  MachineFunction *MF = nullptr;

  bool processLoop(MachineLoop &L);
  void findUsedVectorRegs(MachineLoop &L, BitVector &UsedRegs);
  void insertZeroingInstructions(MachineLoop &L, const BitVector &UsedRegs);
  bool isCallerSavedVectorReg(MCRegister Reg) const;
};

char AArch64VectorRegZeroing::ID = 0;

} // end anonymous namespace

INITIALIZE_PASS_BEGIN(AArch64VectorRegZeroing, DEBUG_TYPE,
                      "AArch64 Vector Register Zeroing", false, false)
INITIALIZE_PASS_DEPENDENCY(MachineLoopInfoWrapperPass)
INITIALIZE_PASS_END(AArch64VectorRegZeroing, DEBUG_TYPE,
                    "AArch64 Vector Register Zeroing", false, false)

FunctionPass *llvm::createAArch64VectorRegZeroingPass() {
  return new AArch64VectorRegZeroing();
}

bool AArch64VectorRegZeroing::runOnMachineFunction(MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "***** AArch64VectorRegZeroing *****\n");
  LLVM_DEBUG(dbgs() << "Function: " << MF.getName() << '\n');

  if (skipFunction(MF.getFunction()))
    return false;

  this->MF = &MF;
  TII = static_cast<const AArch64InstrInfo *>(MF.getSubtarget().getInstrInfo());
  TRI = MF.getSubtarget().getRegisterInfo();

  MachineLoopInfo &LI = getAnalysis<MachineLoopInfoWrapperPass>().getLI();

  bool Modified = false;

  // Only process outermost loops - zeroing is not free, so we don't want to
  // do it repeatedly for nested loops. Zeroing before the outermost loop is
  // sufficient to free up physical registers for the entire loop nest.
  for (MachineLoop *L : LI) {
    Modified |= processLoop(*L);
  }

  return Modified;
}

bool AArch64VectorRegZeroing::processLoop(MachineLoop &L) {
  LLVM_DEBUG(dbgs() << "Processing loop at BB#"
                    << L.getHeader()->getNumber() << '\n');

  ++NumLoopsProcessed;

  // Get the loop preheader - we need it to insert zeroing instructions
  MachineBasicBlock *Preheader = L.getLoopPreheader();
  if (!Preheader) {
    LLVM_DEBUG(dbgs() << "  Skipping: no preheader\n");
    ++NumLoopsSkipped;
    return false;
  }

  // Find which vector registers are used in the loop
  BitVector UsedRegs(32); // Q0-Q31
  findUsedVectorRegs(L, UsedRegs);

  // If no vector registers are used in the loop, skip zeroing
  if (UsedRegs.none()) {
    LLVM_DEBUG(dbgs() << "  Skipping: loop doesn't use vector registers\n");
    ++NumLoopsSkipped;
    return false;
  }

  LLVM_DEBUG({
    dbgs() << "  Used Q registers: ";
    for (unsigned i = 0; i < 32; ++i) {
      if (UsedRegs[i])
        dbgs() << "Q" << i << " ";
    }
    dbgs() << '\n';
  });

  // Insert zeroing instructions for unused caller-saved registers
  insertZeroingInstructions(L, UsedRegs);

  return true;
}

void AArch64VectorRegZeroing::findUsedVectorRegs(MachineLoop &L,
                                                  BitVector &UsedRegs) {
  // Scan all instructions in the loop to find which Q registers are used
  for (MachineBasicBlock *MBB : L.getBlocks()) {
    for (MachineInstr &MI : *MBB) {
      for (MachineOperand &MO : MI.operands()) {
        if (!MO.isReg())
          continue;

        Register Reg = MO.getReg();
        if (!Reg.isPhysical())
          continue;

        // Check if this is a Q register (FPR128 class contains Q0-Q31)
        if (AArch64::FPR128RegClass.contains(Reg)) {
          unsigned QNum = Reg - AArch64::Q0;
          assert(QNum < 32 && "Q register index out of range");
          UsedRegs.set(QNum);
        }
        // Also check for subregs (D, S, H, B) which map to Q registers
        else if (AArch64::FPR64RegClass.contains(Reg)) {
          unsigned DNum = Reg - AArch64::D0;
          UsedRegs.set(DNum); // D0 maps to Q0, etc.
        } else if (AArch64::FPR32RegClass.contains(Reg)) {
          unsigned SNum = Reg - AArch64::S0;
          UsedRegs.set(SNum);
        } else if (AArch64::FPR16RegClass.contains(Reg)) {
          unsigned HNum = Reg - AArch64::H0;
          UsedRegs.set(HNum);
        } else if (AArch64::FPR8RegClass.contains(Reg)) {
          unsigned BNum = Reg - AArch64::B0;
          UsedRegs.set(BNum);
        }
      }
    }
  }
}

bool AArch64VectorRegZeroing::isCallerSavedVectorReg(MCRegister Reg) const {
  assert(MF && "MachineFunction must be set");
  // For AArch64 AAPCS: D8-D15 (lower 64 bits of Q8-Q15) are callee-saved.
  // Q0-Q7 and Q16-Q31 are caller-saved.
  // We need to check if this register or any of its sub-registers are callee-saved.
  // Since D8-D15 are sub-registers of Q8-Q15, we must not zero Q8-Q15.

  if (TRI->isCalleeSavedPhysReg(Reg, *MF))
    return false;

  // Check if any sub-register is callee-saved
  for (MCPhysReg SubReg : TRI->subregs_inclusive(Reg)) {
    if (TRI->isCalleeSavedPhysReg(SubReg, *MF))
      return false;
  }

  return true;
}

void AArch64VectorRegZeroing::insertZeroingInstructions(
    MachineLoop &L, const BitVector &UsedRegs) {
  MachineBasicBlock *Preheader = L.getLoopPreheader();
  assert(Preheader && "Preheader should exist");

  // Insert before the terminator (branch to loop header)
  MachineBasicBlock::iterator InsertPt = Preheader->getFirstTerminator();
  DebugLoc DL;

  unsigned NumZeroed = 0;

  // Zero out unused caller-saved registers
  for (unsigned Q = 0; Q < 32; ++Q) {
    if (UsedRegs[Q])
      continue; // Register is used in the loop

    MCRegister QReg = AArch64::Q0 + Q;
    if (!isCallerSavedVectorReg(QReg))
      continue; // Skip callee-saved Q8-Q15

    // Insert: movi vN.2d, #0
    // This is the canonical way to zero a 128-bit vector register
    BuildMI(*Preheader, InsertPt, DL, TII->get(AArch64::MOVIv2d_ns), QReg)
        .addImm(0);

    LLVM_DEBUG(dbgs() << "  Inserted: movi v" << Q << ".2d, #0\n");
    ++NumZeroed;
  }

  NumRegistersZeroed += NumZeroed;

  LLVM_DEBUG(dbgs() << "  Total registers zeroed: " << NumZeroed << '\n');
}
