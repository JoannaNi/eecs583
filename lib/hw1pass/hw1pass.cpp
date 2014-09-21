#define DEBUG_TYPE "hello"
#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Analysis/ProfileInfo.h"
#include "llvm/Analysis/BranchProbabilityInfo.h"
using namespace llvm;

#include<string>
using namespace std;
STATISTIC(HelloCounter, "Counts number of functions greeted");

namespace {
  // statComp, computes count of each instruction type
  struct statComp : public FunctionPass {
    static char ID; // Pass identification, replacement for typeid
    ProfileInfo* PI;
    BranchProbabilityInfo* BPI;
    enum InstructionType { IntegerALU, FloatALU, Memory, Branch, Other };

    InstructionType classifyInstruction(Instruction* i) {
      string opcodeName = string(i->getOpcodeName());
      enum InstructionType instructionType;
      if (opcodeName == "br" || opcodeName == "switch" || opcodeName == "indirectbr") {
	instructionType = Branch;
      }
      else if (opcodeName == "add" || opcodeName == "sub" || opcodeName == "mul" || opcodeName == "udiv" || opcodeName == "sdiv" || opcodeName == "urem" || opcodeName == "shl" || opcodeName == "lshr" || opcodeName == "ashr" || opcodeName == "and" || opcodeName == "or" || opcodeName == "xor" || opcodeName == "icmp" || opcodeName == "srem") {
	instructionType = IntegerALU;
      }
      else if (opcodeName == "fadd" || opcodeName == "fsub" || opcodeName == "fmul" || opcodeName == "fdiv" || opcodeName == "frem" || opcodeName == "fcmp") {
	instructionType = FloatALU;
      }
      else if (opcodeName == "alloca" || opcodeName == "load" || opcodeName == "store" || opcodeName == "getelementptr" || opcodeName == "fence" || opcodeName == "cmpxchg" || opcodeName == "atomicrmw") {
	instructionType = Memory;
      }
      else {
	instructionType = Other;
      }
      return instructionType;
    }

    statComp() : FunctionPass(ID) {}

    virtual bool runOnFunction(Function &F) {
      PI = &getAnalysis<ProfileInfo>();
      BPI = &getAnalysis<BranchProbabilityInfo>();
      double integerALUCount = 0;
      double floatALUCount = 0;
      double memoryCount = 0;
      double biasedBranchCount = 0;
      double unbiasedBranchCount = 0;
      double otherCount = 0;

      for (Function::iterator b = F.begin(), be = F.end(); b != be; ++b) {
	int executionCount = (int)PI->getExecutionCount(b);
	errs() << "execution count of current basic block: " << executionCount << '\n';
	if (executionCount > 0){
	  for (BasicBlock::iterator i = b->begin(), ie = b->end(); i != ie; i++) {
	    enum InstructionType instructionType = classifyInstruction((Instruction*) i);
	    assert (instructionType >= 0 && instructionType <= 5);
	    switch (instructionType) {
	    case 0: 
	      integerALUCount += executionCount;
	      break;
	    case 1: 
	      floatALUCount += executionCount;
	      break;
	    case 2:
	      memoryCount += executionCount;
	      break;
	    case 3:
	      // check if it's biased or unbiased using BPI
	      if (BPI->getHotSucc((BasicBlock*) b)){
		biasedBranchCount += executionCount;
	      }
	      else {
		unbiasedBranchCount += executionCount;
	      }
	      break;
	    case 4:
	      otherCount += executionCount;
	      break;
	    }
	  }
	}
      }
      errs() << "integerALUCount: " << integerALUCount << '\n';
      errs() << "floatALUCount: " << floatALUCount << '\n';
      errs() << "memoryCount: " << memoryCount << '\n';
      errs() << "biasedBranchALUCount: " << biasedBranchCount << '\n';
      errs() << "unbiasedBranchCount: " << unbiasedBranchCount << '\n';
      errs() << "otherCount: " << otherCount << '\n';

      errs() << "runOnFunction getting called on function:" << '\n';
      errs().write_escaped(F.getName()) << '\n';
      return false;
    }


    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<ProfileInfo>();
      AU.addRequired<BranchProbabilityInfo>();
    }
  };
}

char statComp::ID = 0;
static RegisterPass<statComp> Y("hw1pass", "Hello World Pass (with getAnalysisUsage implemented)");
