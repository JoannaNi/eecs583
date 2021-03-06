// TODO:  worry about splitting preheader or put code at end of existing preheader

//===-- SLICM.cpp - Loop Invariant Code Motion Pass ------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// EECS583 F14 - This pass can be used as a template for your Speculative LICM
//               homework assignment.  The pass gets registered as "slicm".
//               This code is almost identical to the LICM.cpp that exists in
//               LLVM 3.3, just with the name of the pass changed to SLICM, and 
//               with certain lines commented out to allow this transformation
//               to be build outside of the LLVM codebase.
//               
//
// This pass performs loop invariant code motion, attempting to remove as much
// code from the body of a loop as possible.  It does this by either hoisting
// code into the preheader block, or by sinking code to the exit blocks if it is
// safe.  This pass also promotes must-aliased memory locations in the loop to
// live in registers, thus hoisting and sinking "invariant" loads and stores.
//
// This pass uses alias analysis for two purposes:
//
//  1. Moving loop invariant loads and calls out of loops.  If we can determine
//     that a load or call inside of a loop never aliases anything stored to,
//     we can hoist it or sink it like any other instruction.
//  2. Scalar Promotion of Memory - If there is a store instruction inside of
//     the loop, we try to move the store to happen AFTER the loop instead of
//     inside of the loop.  This can only happen if a few conditions are true:
//       A. The pointer stored through is loop invariant
//       B. There are no stores or loads in the loop which _may_ alias the
//          pointer.  There are no calls in the loop which mod/ref the pointer.
//     If these conditions are true, we can promote the loads and stores in the
//     loop of the pointer to use a temporary alloca'd variable.  We then use
//     the SSAUpdater to construct the appropriate SSA form for the value.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "slicm"
#include "llvm/Transforms/Scalar.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/AliasSetTracker.h"
#include "llvm/Analysis/ConstantFolding.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/Support/CFG.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetLibraryInfo.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/SSAUpdater.h"
#include <algorithm>

// SLICM includes
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "LAMP/LAMPLoadProfile.h"
#include "llvm/Analysis/ProfileInfo.h"

using namespace llvm;

#include <string>
#include <set>
#include <vector>
using namespace std;

STATISTIC(NumSunk      , "Number of instructions sunk out of loop");
STATISTIC(NumHoisted   , "Number of instructions hoisted out of loop");
STATISTIC(NumMovedLoads, "Number of load insts hoisted or sunk");
STATISTIC(NumMovedCalls, "Number of call insts hoisted or sunk");
STATISTIC(NumPromoted  , "Number of memory locations promoted to registers");

static cl::opt<bool>
DisablePromotion("disable-slicm-promotion", cl::Hidden,
                 cl::desc("Disable memory promotion in SLICM pass"));

namespace {
  struct SLICM : public LoopPass {
    static char ID; // Pass identification, replacement for typeid
    SLICM() : LoopPass(ID) {
      //initializeSLICMPass(*PassRegistry::getPassRegistry()); // 583 - commented out
    }

    virtual bool runOnLoop(Loop *L, LPPassManager &LPM);

    /// This transformation requires natural loop information & requires that
    /// loop preheaders be inserted into the CFG...
    ///
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
//      AU.setPreservesCFG();                   // 583 - commented out
      AU.addRequired<DominatorTree>();
      AU.addRequired<LoopInfo>();
//      AU.addRequiredID(LoopSimplifyID);       // 583 - commented out
      AU.addRequired<AliasAnalysis>();
//      AU.addPreserved<AliasAnalysis>();       // 583 - commented out
//      AU.addPreserved("scalar-evolution");    // 583 - commented out
//      AU.addPreservedID(LoopSimplifyID);      // 583 - commented out
      AU.addRequired<TargetLibraryInfo>();
      AU.addRequired<ProfileInfo>();
      AU.addRequired<LAMPLoadProfile>();
    }

    using llvm::Pass::doFinalization;

    bool doFinalization() {
      assert(LoopToAliasSetMap.empty() && "Didn't free loop alias sets");
      return false;
    }

  private:
    AliasAnalysis *AA;       // Current AliasAnalysis information
    LoopInfo      *LI;       // Current LoopInfo
    DominatorTree *DT;       // Dominator Tree for the current Loop.

    DataLayout *TD;          // DataLayout for constant folding.
    TargetLibraryInfo *TLI;  // TargetLibraryInfo for constant folding.
    ProfileInfo * PI;
    LAMPLoadProfile* LLP;
    // State that is updated as we process loops.
    bool Changed;            // Set to true when we change anything.
    BasicBlock *Preheader;   // The preheader block of the current loop...
    Loop *CurLoop;           // The current loop we are working on...
    AliasSetTracker *CurAST; // AliasSet information for the current loop...
    bool MayThrow;           // The current loop contains an instruction which
                             // may throw, thus preventing code motion of
                             // instructions with side effects.
    DenseMap<Loop*, AliasSetTracker*> LoopToAliasSetMap;

    /// cloneBasicBlockAnalysis - Simple Analysis hook. Clone alias set info.
    void cloneBasicBlockAnalysis(BasicBlock *From, BasicBlock *To, Loop *L);

    /// deleteAnalysisValue - Simple Analysis hook. Delete value V from alias
    /// set.
    void deleteAnalysisValue(Value *V, Loop *L);

    /// SinkRegion - Walk the specified region of the CFG (defined by all blocks
    /// dominated by the specified block, and that are in the current loop) in
    /// reverse depth first order w.r.t the DominatorTree.  This allows us to
    /// visit uses before definitions, allowing us to sink a loop body in one
    /// pass without iteration.
    ///
    void SinkRegion(DomTreeNode *N);

    /// HoistRegion - Walk the specified region of the CFG (defined by all
    /// blocks dominated by the specified block, and that are in the current
    /// loop) in depth first order w.r.t the DominatorTree.  This allows us to
    /// visit definitions before uses, allowing us to hoist a loop body in one
    /// pass without iteration.
    ///
    void HoistRegion(DomTreeNode *N);
    
    /// SLICM function declarations
    /// Function to call after loads are hoisted during speculative part
    set<Instruction*> SecondHoistRegion(DomTreeNode *N);

    /// find all hoistable loads
    set<Instruction*> findAllHoistableLoads(Loop *L, LoopInfo *LI);
    /// find all stores
    set<StoreInst*> findAllStores(Loop *L);
    /// create map from each load to corresponding redo block, and hoist out the loads
    map <Instruction*, pair<BasicBlock*,AllocaInst*> > createLoadToRedoBBMap(set<Instruction*> hoistableLoads, BasicBlock *Entry);

    /// inSubLoop - Little predicate that returns true if the specified basic
    /// block is in a subloop of the current one, not the current one itself.
    ///
    bool inSubLoop(BasicBlock *BB) {
      assert(CurLoop->contains(BB) && "Only valid if BB is IN the loop");
      return LI->getLoopFor(BB) != CurLoop;
    }

    /// sink - When an instruction is found to only be used outside of the loop,
    /// this function moves it to the exit blocks and patches up SSA form as
    /// needed.
    ///
    void sink(Instruction &I);

    /// hoist - When an instruction is found to only use loop invariant operands
    /// that is safe to hoist, this instruction is called to do the dirty work.
    ///
    void hoist(Instruction &I);

    /// isSafeToExecuteUnconditionally - Only sink or hoist an instruction if it
    /// is not a trapping instruction or if it is a trapping instruction and is
    /// guaranteed to execute.
    ///
    bool isSafeToExecuteUnconditionally(Instruction &I);

    /// isGuaranteedToExecute - Check that the instruction is guaranteed to
    /// execute.
    ///
    bool isGuaranteedToExecute(Instruction &I);

    /// pointerInvalidatedByLoop - Return true if the body of this loop may
    /// store into the memory location pointed to by V.
    ///
    bool pointerInvalidatedByLoop(Value *V, uint64_t Size,
                                  const MDNode *TBAAInfo) {
      // Check to see if any of the basic blocks in CurLoop invalidate *V.
      return CurAST->getAliasSetForPointer(V, Size, TBAAInfo).isMod();
    }

    bool canSinkOrHoistInst(Instruction &I);
    bool canHoistLoad(Instruction &I);
    bool isNotUsedInLoop(Instruction &I);

    void PromoteAliasSet(AliasSet &AS,
                         SmallVectorImpl<BasicBlock*> &ExitBlocks,
                         SmallVectorImpl<Instruction*> &InsertPts);
  };
}

char SLICM::ID = 0;
/*
// 583 - commented out INITIALIZE_ macros & createSLICMPass
INITIALIZE_PASS_BEGIN(SLICM, "slicm", "Loop Invariant Code Motion", false, false)
INITIALIZE_PASS_DEPENDENCY(DominatorTree)
INITIALIZE_PASS_DEPENDENCY(LoopInfo)
INITIALIZE_PASS_DEPENDENCY(LoopSimplify)
INITIALIZE_PASS_DEPENDENCY(TargetLibraryInfo)
INITIALIZE_AG_DEPENDENCY(AliasAnalysis)
INITIALIZE_PASS_END(SLICM, "slicm", "Loop Invariant Code Motion", false, false)

Pass *llvm::createSLICMPass() { return new SLICM(); }
*/
static RegisterPass<SLICM> X("slicm", "Speculative Loop Invariant Code Motion");

/// Hoist expressions out of the specified loop. Note, alias info for inner
/// loop is not preserved so it is not a good idea to run SLICM multiple
/// times on one loop.
///

set<Instruction*> SLICM::findAllHoistableLoads(Loop* L, LoopInfo *LI){
  set<Instruction*> hoistableLoads;
  for (Loop::block_iterator I = L->block_begin(), E = L->block_end();
       I != E; ++I) {
    BasicBlock *BB = *I;
    if (LI->getLoopFor(BB) == L) {       // Ignore blocks in subloops.
      for (BasicBlock::iterator i = BB->begin(), ie = BB->end(); i!= ie; ) {
	Instruction &I = *i++;
	string opcodeName = string(I.getOpcodeName());
	//errs() << "Opcode Name: " << opcodeName << '\n';

	if (opcodeName == "load" and canHoistLoad(I)) {
	  hoistableLoads.insert(&I);
	}
      }
    }
  }
  return hoistableLoads;
}

set<StoreInst*> SLICM::findAllStores(Loop *L) {
  set<StoreInst*> allStores;
  for (Loop::block_iterator I = L->block_begin(), E = L->block_end(); I != E; ++I) {
    BasicBlock *BB = *I;
    for (BasicBlock::iterator i = BB->begin(), ie = BB->end(); i!= ie; ) {
      Instruction &I = *i++;
      string opcodeName = string(I.getOpcodeName());
      if (opcodeName == "store") {
	allStores.insert((StoreInst*)&I);
      }
    }
  }
  return allStores;
}

map <Instruction*, pair<BasicBlock*,AllocaInst*> > SLICM::createLoadToRedoBBMap(set<Instruction*> hoistableLoads, BasicBlock *Entry) {
  // split basic block of each hoistable load, create redo block and rest block for each
  // create a flag for each load, hoist load. 
  // create map of loads to redoBB's and associated flag
  map <Instruction*, pair<BasicBlock*,AllocaInst*> > loadToRedoBlockMap;
  set<Instruction*>::iterator sit;
  for (set<Instruction*>::iterator sit = hoistableLoads.begin(), sie = hoistableLoads.end(); sit != sie; ) {
    Instruction &I = *(*sit++);
    string opcodeName = string(I.getOpcodeName());
    //errs() << "Opcode Name: " << opcodeName << '\n';
    BasicBlock *BB = I.getParent();
    if (opcodeName == "load" and canHoistLoad(I)) {
      // split bb here
      BasicBlock* redoBlock = SplitBlock(BB, &I, this);
      // redoBlock should point to restBlock
      BasicBlock* restBlock = SplitEdge(BB, redoBlock, this);
      Instruction *oldTerminator = BB->getTerminator();

      // allocate a flag
      AllocaInst *flag = new AllocaInst(Type::getInt1Ty(Entry->getContext()), "flag", Entry->getTerminator());
      StoreInst *ST = new StoreInst(ConstantInt::getFalse(Entry->getContext()), flag, Entry->getTerminator());
      // add branch to end of first bb,
      //// this puts the loadinst at the back of the bb
      LoadInst *LD = new LoadInst(flag, "loadflag", oldTerminator);
      // load the flag into flag, and branch depending on value of flag
      // if flag is true, which means alias occurs, should branch to fixup block
      BranchInst::Create(redoBlock, restBlock, LD, oldTerminator);
      // insert pair of load instruction, redo block to map
      loadToRedoBlockMap.insert(pair<Instruction*, pair<BasicBlock*, AllocaInst*> >(&I, pair<BasicBlock*, AllocaInst*> (redoBlock, flag)));
      oldTerminator->eraseFromParent();
      hoist(I);
    }
  } 
  return loadToRedoBlockMap;
}

bool SLICM::runOnLoop(Loop *L, LPPassManager &LPM) {
  Changed = false;

  // Get our Loop and Alias Analysis information...
  LI = &getAnalysis<LoopInfo>();
  AA = &getAnalysis<AliasAnalysis>();
  DT = &getAnalysis<DominatorTree>();

  TD = getAnalysisIfAvailable<DataLayout>();
  TLI = &getAnalysis<TargetLibraryInfo>();
  PI = &getAnalysis<ProfileInfo>();
  LLP = &getAnalysis<LAMPLoadProfile>();
  CurAST = new AliasSetTracker(*AA);
  // Collect Alias info from subloops.
  for (Loop::iterator LoopItr = L->begin(), LoopItrE = L->end();
       LoopItr != LoopItrE; ++LoopItr) {
    Loop *InnerL = *LoopItr;
    AliasSetTracker *InnerAST = LoopToAliasSetMap[InnerL];
    assert(InnerAST && "Where is my AST?");

    // What if InnerLoop was modified by other passes ?
    CurAST->add(*InnerAST);

    // Once we've incorporated the inner loop's AST into ours, we don't need the
    // subloop's anymore.
    delete InnerAST;
    LoopToAliasSetMap.erase(InnerL);
  }

  CurLoop = L;

  // Get the preheader block to move instructions into...
  Preheader = L->getLoopPreheader();

  // Loop over the body of this loop, looking for calls, invokes, and stores.
  // Because subloops have already been incorporated into AST, we skip blocks in
  // subloops.
  //
  for (Loop::block_iterator I = L->block_begin(), E = L->block_end();
       I != E; ++I) {
    BasicBlock *BB = *I;
    if (LI->getLoopFor(BB) == L)        // Ignore blocks in subloops.
      CurAST->add(*BB);                 // Incorporate the specified basic block
  }

  MayThrow = false;
  // TODO: We've already searched for instructions which may throw in subloops.
  // We may want to reuse this information.
  for (Loop::block_iterator BB = L->block_begin(), BBE = L->block_end();
       (BB != BBE) && !MayThrow ; ++BB)
    for (BasicBlock::iterator I = (*BB)->begin(), E = (*BB)->end();
         (I != E) && !MayThrow; ++I)
      MayThrow |= I->mayThrow();

  // We want to visit all of the instructions in this loop... that are not parts
  // of our subloops (they have already had their invariants hoisted out of
  // their loop, into this loop, so there is no need to process the BODIES of
  // the subloops).
  //
  // Traverse the body of the loop in depth first order on the dominator tree so
  // that we are guaranteed to see definitions before we see uses.  This allows
  // us to sink instructions in one pass, without iteration.  After sinking
  // instructions, we perform another pass to hoist them out of the loop.
  //
  if (L->hasDedicatedExits())
    SinkRegion(DT->getNode(L->getHeader()));
  if (Preheader)
    HoistRegion(DT->getNode(L->getHeader()));

  // Now that all loop invariants have been removed from the loop, promote any
  // memory references to scalars that we can.
  if (!DisablePromotion && Preheader && L->hasDedicatedExits()) {
    SmallVector<BasicBlock *, 8> ExitBlocks;
    SmallVector<Instruction *, 8> InsertPts;

    // Loop over all of the alias sets in the tracker object.
    for (AliasSetTracker::iterator I = CurAST->begin(), E = CurAST->end();
         I != E; ++I)
      PromoteAliasSet(*I, ExitBlocks, InsertPts);
  }


  // SLICM code starts here
  // move out all of the load instructions from the loop
  // eventually get the dependency mapping used in hw1
  Function *parentFunction = Preheader->getParent();
  Function::iterator fit = parentFunction->getEntryBlock();
  BasicBlock* Entry = (BasicBlock*) fit;

  // first find all the loads we can hoist
  set <Instruction*> hoistableLoads = findAllHoistableLoads(L, LI);
  // get all the stores, so that we can check for aliasing later
  // TODO: stores might have to be global
  set <StoreInst*> allStores = findAllStores(L);
  // this next function hoists out each load and creates redo block for it (as well as alias flag), stores it in map
  map <Instruction*, pair<BasicBlock*,AllocaInst*> > loadToRedoBlockMap = createLoadToRedoBBMap(hoistableLoads, Entry);
  //now that loads have been hoisted out, check if anything else has become invariant just by running another pass
  set<Instruction*> secondHoistedInstructions = SecondHoistRegion(DT->getNode(L->getHeader()));
  set<Instruction*>::iterator it;  
  map<Instruction*, pair<BasicBlock*, AllocaInst*> >::iterator mit;
  // we have list of instructions which were hoisted after loads were hoisted
  // and map from hoisted load to redo basic block
  for (it = secondHoistedInstructions.begin();  it != secondHoistedInstructions.end(); ++it) {
    //errs() << "Hoisted instruction: " << **it << '\n';
  }

  
  // to handle ssa form, each load needs to be modified as follows:  An allocainst needs to be added to the entry block of the function, 
  // allocating a stack variable corresponding to the load.  then after the load, we add a store which stores the loaded value into the 
  // allocated stack variable.  now every consumer of the load should also use that stack variable instead of the original loaded register
  // we do this by loading the stack variable into a temporary register, and replacing uses of the load instruction with the temporary register instead
  map<Instruction*, AllocaInst*> instToStackVar; //maps hoisted instruction to its corresponding stack variable
  for (mit = loadToRedoBlockMap.begin(); mit != loadToRedoBlockMap.end(); ++ mit) {
    LoadInst *load = (LoadInst*) mit->first;
    // each time we load, store value to an allocated stack variable.  then any users of that load should read from stack variable instead
    AllocaInst * stackVar = new AllocaInst(load->getType(), "", Entry->begin());
    StoreInst * storeStack = new StoreInst(load, stackVar);
    storeStack->insertAfter(load);
    instToStackVar.insert(pair<Instruction*, AllocaInst*>((Instruction*)load, stackVar));

  }

  // populate the redo blocks by iterating through each hoisted load, finding all of its uses, and checking whether they were hoisted
  // after all the loads were hoisted (meaning they need to be redone if the load was actually aliased)
  // have to fix up SSA form here too.  So in addition to cloning each load, need to modify it such that each cloned load also stores to corresponding stack variable.  then both load and cloned load refer to same stack variable.  but all the re-hoisted instructions are also cloned...
  //
  for (mit = loadToRedoBlockMap.begin();  mit != loadToRedoBlockMap.end(); ++mit) {
    // for each hoisted load, check its use list
    LoadInst *load = (LoadInst*) mit->first;
    BasicBlock *redoBlock = mit->second.first;
    AllocaInst *flag = mit->second.second;
    // first insert copy of load into redo block
    
    LoadInst* copyLoad = (LoadInst*) load->clone();
    copyLoad->insertBefore(redoBlock->getTerminator());
    // SSA form
    StoreInst* storeStack = new StoreInst(copyLoad, instToStackVar[(Instruction*)load]);
    storeStack->insertAfter(copyLoad);
    // make set of instructions which depend on load, even indirect dependencies
    set<Instruction*> dependentInstructions;

    for (Instruction::use_iterator u = load->use_begin(); u != load->use_end(); u++) {
      // first insert all the directly dependent instructions
      Instruction* directUser = (Instruction*)*u;
      dependentInstructions.insert(directUser);
    }
    // for each instruction which depends on the load, check for instructions which depend on it which were also hoisted
    // and add them to the set if they were.  Then copy instruction and put it into redo block
    // TODO: Stores have no type, so a store which was hoisted and is a user will break stuff
    // this block does preliminary stuff for ssa (allocate and store instruction) and clones each of the hoisted instructions to the
    // correct locations.  also add a store for each hoisted instruction to store that to a new item
    while (!dependentInstructions.empty()) {
      set<Instruction*>::iterator sit = dependentInstructions.begin();
      Instruction* inst = *sit;

      // fix up the second hoisted instructions here too for ssa form too
      // which means each of the return values should also have an alloca inst in the first function block
      // and the result of the instruction should be stored to that stack variable again
      // errs() << *(inst->getType()) << '\n';
      // errs() << *inst << '\n';

      if (secondHoistedInstructions.find(inst) != secondHoistedInstructions.end()) {
	// check if instruction already has an associated stack variable
	AllocaInst *stackVar;
	if (instToStackVar.find(inst) != instToStackVar.end()) {
	  // already has associated stack variable
	  stackVar = instToStackVar[inst];
	  // errs() << "DIFFERS FROM OLD VERSION" << '\n';
	}
	else {
	  stackVar = new AllocaInst(inst->getType(), "", Entry->begin());
	  instToStackVar.insert(pair<Instruction*, AllocaInst*>(inst, stackVar));
	}
	StoreInst * storeStack = new StoreInst(inst, stackVar);
	storeStack->insertAfter(inst);
	// copy instruction into redo block
	Instruction* copyInst = inst->clone();
	copyInst->insertBefore(redoBlock->getTerminator());
	// copied instruction should also store to the stack variable
	StoreInst* copyStore = new StoreInst(copyInst, instToStackVar[inst]);
	copyStore->insertAfter(copyInst);
	//errs() << "copying dependent instruction to redo block: " << *copyInst << '\n';
      }
      
      // check if any of the users of inst were also hoisted, in which case they are a n+1 level dependency
      // and should be inserted into dependent instructions also
      // TODO: somehow have to preserve the order that these instructions are inserted
      for (Instruction::use_iterator u = inst->use_begin(); u != inst->use_end(); u++) {
	Instruction * curInst = (Instruction*)*u;
	if (secondHoistedInstructions.find(curInst) != secondHoistedInstructions.end()) {
	  dependentInstructions.insert(curInst);
	}
      }
      dependentInstructions.erase(sit);
    }

    // clear the flag!
    StoreInst *flagClear = new StoreInst(ConstantInt::getFalse(redoBlock->getContext()), flag, redoBlock->getTerminator());
  }

  // make one last pass through all the users of the hoisted loads/instructions, replacing all operands of any uses
  // skip users in the preheader, since those are the stores we created up there.  why?

  // first iterate through hoisted loads

  // fix up ssa stuff here. so for each use of a hoisted load replace with using temp register
  vector <pair<Instruction*, pair<Instruction*, Instruction*> > >usersToReplaceOperands;
  vector <pair<Instruction*, pair<Instruction*, Instruction*> > >::iterator iit;
  for (mit = loadToRedoBlockMap.begin();  mit != loadToRedoBlockMap.end(); ++mit) {
    Instruction* hoistedLoad = mit->first;
    for (Instruction::use_iterator u = hoistedLoad->use_begin(); u != hoistedLoad->use_end(); u++) {
      	Instruction * user = (Instruction*)*u;
	// replace uses of hoisted instruction in user instruction
	if (user->getParent() != Preheader) {
	  LoadInst *tempReg = new LoadInst(instToStackVar[hoistedLoad], "tempregister",  user);
	  usersToReplaceOperands.push_back(pair<Instruction*, pair<Instruction*, Instruction*> > (user, pair<Instruction*, Instruction*> (hoistedLoad, tempReg)));
	}
    }
  }


  // do the hoisted instructions too
  for (it = secondHoistedInstructions.begin();  it != secondHoistedInstructions.end(); ++it) {
    Instruction* hoistedInst = *it;
    for (Instruction::use_iterator u = hoistedInst->use_begin(); u != hoistedInst->use_end(); ++u) {
      	Instruction * user = (Instruction*)*u;
	// replace uses of hoisted instruction in user instruction
	if (user->getParent() != Preheader) {
	  LoadInst *tempReg = new LoadInst(instToStackVar[hoistedInst], "tempregister",  user);
	  usersToReplaceOperands.push_back(pair<Instruction*, pair<Instruction*, Instruction*> > (user, pair<Instruction*, Instruction*> (hoistedInst, tempReg)));
	}
    }
  }
  
  for (iit = usersToReplaceOperands.begin(); iit != usersToReplaceOperands.end(); ++ iit) {
    Instruction* user = iit->first;
    Instruction* hoistedInst = iit->second.first;
    Instruction* tempReg = iit->second.second;
    user->replaceUsesOfWith(hoistedInst, tempReg);
  }
  
  // check for alias here
  // for each load, check all existing stores
  // for each load it matches, set appropriate flag to true
  // TODO: also set stores map to be global
  set<StoreInst*>::iterator sit;
  for (mit = loadToRedoBlockMap.begin(); mit != loadToRedoBlockMap.end(); ++mit) {
      // or the result of the existing load flag with itself and the new comparison
      Instruction* hoistedLoad = mit->first;
      BasicBlock* redoBlock = mit->second.first;
      AllocaInst *flag = mit->second.second;
      Value* load_operand = hoistedLoad->getOperand(0);
      errs() << "load operand 0: " << *load_operand << '\n';
      // PtrToIntInst *loadLoc = new PtrToIntInst(load_operand, Type::getInt64Ty(redoBlock->getParent()->getContext()),"conv");
      for (sit = allStores.begin(); sit != allStores.end(); ++sit) {
	StoreInst* store = *sit;
	Value* store_operand = store->getOperand(1);
	errs() << "store operand 1: " << *store_operand << '\n';
	// PtrToIntInst *storeLoc = new PtrToIntInst(store_operand, Type::getInt64Ty(redoBlock->getParent()->getContext()),"conv");
	
	// ICmpInst *aliasCheck = new ICmpInst(ICmpInst::ICMP_EQ, loadLoc, storeLoc, "cmp");
	if (load_operand->getType() == store_operand->getType()) {
	  // compare store destination and load source with aliasCheck, pull old flag val with oldFlagVal
	  // store aliasCheck result into new stack variable, pull into aliasVal
	  // or aliasVal and oldFlagVal, store result back into flag
	  errs() << "matching type\n";
	  // aliasCheck contains equality of load operand and store operand
	  ICmpInst *aliasCheck = new ICmpInst(ICmpInst::ICMP_EQ, load_operand, store_operand, "cmp");
	  errs() << "alias check: " << *aliasCheck << '\n';
	  aliasCheck->insertAfter(store);
	  // if alias check or the flag is already set, flag should be set to true after this
	  // oldFlagVal loads value of flag
	  LoadInst *oldFlagVal = new LoadInst(flag, "oldFlagVal");
	  errs() << "old flag val: " << *oldFlagVal << '\n';
	  oldFlagVal->insertAfter(aliasCheck);
	  // allocate stack variable to put value of aliascheck
	  AllocaInst *aliasCheckStackVar = new AllocaInst(aliasCheck->getType(),"aliasCheckStackVar");
	  aliasCheckStackVar->insertAfter(oldFlagVal);
	  // store aliascheck value into aliascheckstackvar
	  StoreInst *setAliasCheckStackVar = new StoreInst(aliasCheck, aliasCheckStackVar);
	  setAliasCheckStackVar->insertAfter(aliasCheckStackVar);
	  // load aliascheck stack variable into aliasVal
	  LoadInst *aliasVal = new LoadInst(aliasCheckStackVar, "aliasVal");
	  aliasVal->insertAfter(setAliasCheckStackVar);
	  errs() << "alias value: " << *aliasVal << '\n';
	  BinaryOperator* flagValue = BinaryOperator::Create(Instruction::Or, aliasVal, oldFlagVal, "flag.ORed");
	  flagValue->insertAfter(aliasVal);
	  errs() << "binary or instruction: " << *flagValue << '\n';
	  // update the correct flag
	  StoreInst *flagUpdate = new StoreInst(flagValue, flag);
	  errs() << "store updated flag value: " << *flagUpdate << '\n';
	  flagUpdate->insertAfter(flagValue);
	}
      }
  }
  // end mark stuff


  // Clear out loops state information for the next iteration
  CurLoop = 0;
  Preheader = 0;

  // If this loop is nested inside of another one, save the alias information
  // for when we process the outer loop.
  if (L->getParentLoop())
    LoopToAliasSetMap[L] = CurAST;
  else
    delete CurAST;
  return Changed;
}

/// SinkRegion - Walk the specified region of the CFG (defined by all blocks
/// dominated by the specified block, and that are in the current loop) in
/// reverse depth first order w.r.t the DominatorTree.  This allows us to visit
/// uses before definitions, allowing us to sink a loop body in one pass without
/// iteration.
///
void SLICM::SinkRegion(DomTreeNode *N) {
  assert(N != 0 && "Null dominator tree node?");
  BasicBlock *BB = N->getBlock();

  // If this subregion is not in the top level loop at all, exit.
  if (!CurLoop->contains(BB)) return;

  // We are processing blocks in reverse dfo, so process children first.
  const std::vector<DomTreeNode*> &Children = N->getChildren();
  for (unsigned i = 0, e = Children.size(); i != e; ++i)
    SinkRegion(Children[i]);

  // Only need to process the contents of this block if it is not part of a
  // subloop (which would already have been processed).
  if (inSubLoop(BB)) return;

  for (BasicBlock::iterator II = BB->end(); II != BB->begin(); ) {
    Instruction &I = *--II;

    // If the instruction is dead, we would try to sink it because it isn't used
    // in the loop, instead, just delete it.
    if (isInstructionTriviallyDead(&I, TLI)) {
      DEBUG(dbgs() << "SLICM deleting dead inst: " << I << '\n');
      ++II;
      CurAST->deleteValue(&I);
      I.eraseFromParent();
      Changed = true;
      continue;
    }

    // Check to see if we can sink this instruction to the exit blocks
    // of the loop.  We can do this if the all users of the instruction are
    // outside of the loop.  In this case, it doesn't even matter if the
    // operands of the instruction are loop invariant.
    //
    if (isNotUsedInLoop(I) && canSinkOrHoistInst(I)) {
      ++II;
      sink(I);
    }
  }
}

/// HoistRegion - Walk the specified region of the CFG (defined by all blocks
/// dominated by the specified block, and that are in the current loop) in depth
/// first order w.r.t the DominatorTree.  This allows us to visit definitions
/// before uses, allowing us to hoist a loop body in one pass without iteration.
///
void SLICM::HoistRegion(DomTreeNode *N) {
  assert(N != 0 && "Null dominator tree node?");
  BasicBlock *BB = N->getBlock();

  // If this subregion is not in the top level loop at all, exit.
  if (!CurLoop->contains(BB)) return;

  // Only need to process the contents of this block if it is not part of a
  // subloop (which would already have been processed).
  if (!inSubLoop(BB))
    for (BasicBlock::iterator II = BB->begin(), E = BB->end(); II != E; ) {
      Instruction &I = *II++;

      // Try constant folding this instruction.  If all the operands are
      // constants, it is technically hoistable, but it would be better to just
      // fold it.
      if (Constant *C = ConstantFoldInstruction(&I, TD, TLI)) {
        DEBUG(dbgs() << "SLICM folding inst: " << I << "  --> " << *C << '\n');
        CurAST->copyValue(&I, C);
        CurAST->deleteValue(&I);
        I.replaceAllUsesWith(C);
        I.eraseFromParent();
        continue;
      }

      // Try hoisting the instruction out to the preheader.  We can only do this
      // if all of the operands of the instruction are loop invariant and if it
      // is safe to hoist the instruction.
      //
      if (CurLoop->hasLoopInvariantOperands(&I) && canSinkOrHoistInst(I) &&
          isSafeToExecuteUnconditionally(I))
        hoist(I);
    }

  const std::vector<DomTreeNode*> &Children = N->getChildren();
  for (unsigned i = 0, e = Children.size(); i != e; ++i)
    HoistRegion(Children[i]);
}

// Post-Load-Hoist-Region
// After loads have been hoisted out, some instructions may become invariant and we want these to be hoisted out
// to the preheader as well.  Instead of returning void, return the set of hoisted instructions since we'll probably need those later
set<Instruction*> SLICM::SecondHoistRegion(DomTreeNode *N) {
  set<Instruction*> HoistedInstructions;
  assert(N != 0 && "Null dominator tree node?");
  BasicBlock *BB = N->getBlock();

  // If this subregion is not in the top level loop at all, exit.
  if (!CurLoop->contains(BB)) return HoistedInstructions;

  // Only need to process the contents of this block if it is not part of a
  // subloop (which would already have been processed).
  if (!inSubLoop(BB))
    for (BasicBlock::iterator II = BB->begin(), E = BB->end(); II != E; ) {
      Instruction &I = *II++;

      // forget about folding constant instructions, should have already been done

      // Try hoisting the instruction out to the preheader.  We can only do this
      // if all of the operands of the instruction are loop invariant and if it
      // is safe to hoist the instruction.
      //
      if (CurLoop->hasLoopInvariantOperands(&I) && canSinkOrHoistInst(I) &&
          isSafeToExecuteUnconditionally(I)) {
	Instruction* instPointer = &I;
        hoist(I);
	HoistedInstructions.insert(instPointer);
      }
    }

  const std::vector<DomTreeNode*> &Children = N->getChildren();
  for (unsigned i = 0, e = Children.size(); i != e; ++i) {
    set<Instruction*> childHoists = SecondHoistRegion(Children[i]);
    HoistedInstructions.insert(childHoists.begin(), childHoists.end());
  }
  return HoistedInstructions;
}




/// canSinkOrHoistInst - Return true if the hoister and sinker can handle this
/// instruction.
///
bool SLICM::canSinkOrHoistInst(Instruction &I) {
  // Loads have extra constraints we have to verify before we can hoist them.
  if (LoadInst *LI = dyn_cast<LoadInst>(&I)) {
    if (!LI->isUnordered())
      return false;        // Don't hoist volatile/atomic loads!

    // Loads from constant memory are always safe to move, even if they end up
    // in the same alias set as something that ends up being modified.
    if (AA->pointsToConstantMemory(LI->getOperand(0)))
      return true;
    if (LI->getMetadata("invariant.load"))
      return true;

    // Don't hoist loads which have may-aliased stores in loop.
    uint64_t Size = 0;
    if (LI->getType()->isSized())
      Size = AA->getTypeStoreSize(LI->getType());
    return !pointerInvalidatedByLoop(LI->getOperand(0), Size,
                                     LI->getMetadata(LLVMContext::MD_tbaa));
  } 
  else if (CallInst *CI = dyn_cast<CallInst>(&I)) {
    // Don't sink or hoist dbg info; it's legal, but not useful.
    if (isa<DbgInfoIntrinsic>(I))
      return false;

    // Handle simple cases by querying alias analysis.
    AliasAnalysis::ModRefBehavior Behavior = AA->getModRefBehavior(CI);
    if (Behavior == AliasAnalysis::DoesNotAccessMemory)
      return true;
    if (AliasAnalysis::onlyReadsMemory(Behavior)) {
      // If this call only reads from memory and there are no writes to memory
      // in the loop, we can hoist or sink the call as appropriate.
      bool FoundMod = false;
      for (AliasSetTracker::iterator I = CurAST->begin(), E = CurAST->end();
           I != E; ++I) {
        AliasSet &AS = *I;
        if (!AS.isForwardingAliasSet() && AS.isMod()) {
          FoundMod = true;
          break;
        }
      }
      if (!FoundMod) return true;
    }

    // FIXME: This should use mod/ref information to see if we can hoist or
    // sink the call.

    return false;
  }

  // Only these instructions are hoistable/sinkable.
  if (!isa<BinaryOperator>(I) && !isa<CastInst>(I) && !isa<SelectInst>(I) &&
      !isa<GetElementPtrInst>(I) && !isa<CmpInst>(I) &&
      !isa<InsertElementInst>(I) && !isa<ExtractElementInst>(I) &&
      !isa<ShuffleVectorInst>(I) && !isa<ExtractValueInst>(I) &&
      !isa<InsertValueInst>(I))
    return false;

  return isSafeToExecuteUnconditionally(I);
}


bool SLICM::canHoistLoad(Instruction &I) {
  // Loads have extra constraints we have to verify before we can hoist them.
  if (LoadInst *LI = dyn_cast<LoadInst>(&I)) {
    if (!LI->isUnordered())
      return false;        // Don't hoist volatile/atomic loads!

    // Loads from constant memory are always safe to move, even if they end up
    // in the same alias set as something that ends up being modified.
    if (AA->pointsToConstantMemory(LI->getOperand(0)))
      return true;
    if (LI->getMetadata("invariant.load"))
      return true;

    if (CurLoop->hasLoopInvariantOperands(&I) && isSafeToExecuteUnconditionally(I)) {
      return true;
    }
    return false;
  } 
  
  return isSafeToExecuteUnconditionally(I);
}



/// isNotUsedInLoop - Return true if the only users of this instruction are
/// outside of the loop.  If this is true, we can sink the instruction to the
/// exit blocks of the loop.
///
bool SLICM::isNotUsedInLoop(Instruction &I) {
  for (Value::use_iterator UI = I.use_begin(), E = I.use_end(); UI != E; ++UI) {
    Instruction *User = cast<Instruction>(*UI);
    if (PHINode *PN = dyn_cast<PHINode>(User)) {
      // PHI node uses occur in predecessor blocks!
      for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i)
        if (PN->getIncomingValue(i) == &I)
          if (CurLoop->contains(PN->getIncomingBlock(i)))
            return false;
    } else if (CurLoop->contains(User)) {
      return false;
    }
  }
  return true;
}


/// sink - When an instruction is found to only be used outside of the loop,
/// this function moves it to the exit blocks and patches up SSA form as needed.
/// This method is guaranteed to remove the original instruction from its
/// position, and may either delete it or move it to outside of the loop.
///
void SLICM::sink(Instruction &I) {
  DEBUG(dbgs() << "SLICM sinking instruction: " << I << "\n");

  SmallVector<BasicBlock*, 8> ExitBlocks;
  CurLoop->getUniqueExitBlocks(ExitBlocks);

  if (isa<LoadInst>(I)) ++NumMovedLoads;
  else if (isa<CallInst>(I)) ++NumMovedCalls;
  ++NumSunk;
  Changed = true;

  // The case where there is only a single exit node of this loop is common
  // enough that we handle it as a special (more efficient) case.  It is more
  // efficient to handle because there are no PHI nodes that need to be placed.
  if (ExitBlocks.size() == 1) {
    if (!DT->dominates(I.getParent(), ExitBlocks[0])) {
      // Instruction is not used, just delete it.
      CurAST->deleteValue(&I);
      // If I has users in unreachable blocks, eliminate.
      // If I is not void type then replaceAllUsesWith undef.
      // This allows ValueHandlers and custom metadata to adjust itself.
      if (!I.use_empty())
        I.replaceAllUsesWith(UndefValue::get(I.getType()));
      I.eraseFromParent();
    } else {
      // Move the instruction to the start of the exit block, after any PHI
      // nodes in it.
      I.moveBefore(ExitBlocks[0]->getFirstInsertionPt());

      // This instruction is no longer in the AST for the current loop, because
      // we just sunk it out of the loop.  If we just sunk it into an outer
      // loop, we will rediscover the operation when we process it.
      CurAST->deleteValue(&I);
    }
    return;
  }

  if (ExitBlocks.empty()) {
    // The instruction is actually dead if there ARE NO exit blocks.
    CurAST->deleteValue(&I);
    // If I has users in unreachable blocks, eliminate.
    // If I is not void type then replaceAllUsesWith undef.
    // This allows ValueHandlers and custom metadata to adjust itself.
    if (!I.use_empty())
      I.replaceAllUsesWith(UndefValue::get(I.getType()));
    I.eraseFromParent();
    return;
  }

  // Otherwise, if we have multiple exits, use the SSAUpdater to do all of the
  // hard work of inserting PHI nodes as necessary.
  SmallVector<PHINode*, 8> NewPHIs;
  SSAUpdater SSA(&NewPHIs);

  if (!I.use_empty())
    SSA.Initialize(I.getType(), I.getName());

  // Insert a copy of the instruction in each exit block of the loop that is
  // dominated by the instruction.  Each exit block is known to only be in the
  // ExitBlocks list once.
  BasicBlock *InstOrigBB = I.getParent();
  unsigned NumInserted = 0;

  for (unsigned i = 0, e = ExitBlocks.size(); i != e; ++i) {
    BasicBlock *ExitBlock = ExitBlocks[i];

    if (!DT->dominates(InstOrigBB, ExitBlock))
      continue;

    // Insert the code after the last PHI node.
    BasicBlock::iterator InsertPt = ExitBlock->getFirstInsertionPt();

    // If this is the first exit block processed, just move the original
    // instruction, otherwise clone the original instruction and insert
    // the copy.
    Instruction *New;
    if (NumInserted++ == 0) {
      I.moveBefore(InsertPt);
      New = &I;
    } else {
      New = I.clone();
      if (!I.getName().empty())
        New->setName(I.getName()+".le");
      ExitBlock->getInstList().insert(InsertPt, New);
    }

    // Now that we have inserted the instruction, inform SSAUpdater.
    if (!I.use_empty())
      SSA.AddAvailableValue(ExitBlock, New);
  }

  // If the instruction doesn't dominate any exit blocks, it must be dead.
  if (NumInserted == 0) {
    CurAST->deleteValue(&I);
    if (!I.use_empty())
      I.replaceAllUsesWith(UndefValue::get(I.getType()));
    I.eraseFromParent();
    return;
  }

  // Next, rewrite uses of the instruction, inserting PHI nodes as needed.
  for (Value::use_iterator UI = I.use_begin(), UE = I.use_end(); UI != UE; ) {
    // Grab the use before incrementing the iterator.
    Use &U = UI.getUse();
    // Increment the iterator before removing the use from the list.
    ++UI;
    SSA.RewriteUseAfterInsertions(U);
  }

  // Update CurAST for NewPHIs if I had pointer type.
  if (I.getType()->isPointerTy())
    for (unsigned i = 0, e = NewPHIs.size(); i != e; ++i)
      CurAST->copyValue(&I, NewPHIs[i]);

  // Finally, remove the instruction from CurAST.  It is no longer in the loop.
  CurAST->deleteValue(&I);
}

/// hoist - When an instruction is found to only use loop invariant operands
/// that is safe to hoist, this instruction is called to do the dirty work.
///
void SLICM::hoist(Instruction &I) {
  DEBUG(dbgs() << "SLICM hoisting to " << Preheader->getName() << ": "
        << I << "\n");

  // Move the new node to the Preheader, before its terminator.
  I.moveBefore(Preheader->getTerminator());

  if (isa<LoadInst>(I)) ++NumMovedLoads;
  else if (isa<CallInst>(I)) ++NumMovedCalls;
  ++NumHoisted;
  Changed = true;
}

/// isSafeToExecuteUnconditionally - Only sink or hoist an instruction if it is
/// not a trapping instruction or if it is a trapping instruction and is
/// guaranteed to execute.
///
bool SLICM::isSafeToExecuteUnconditionally(Instruction &Inst) {
  // If it is not a trapping instruction, it is always safe to hoist.
  if (isSafeToSpeculativelyExecute(&Inst))
    return true;

  return isGuaranteedToExecute(Inst);
}

bool SLICM::isGuaranteedToExecute(Instruction &Inst) {

  // Somewhere in this loop there is an instruction which may throw and make us
  // exit the loop.
  if (MayThrow)
    return false;

  // Otherwise we have to check to make sure that the instruction dominates all
  // of the exit blocks.  If it doesn't, then there is a path out of the loop
  // which does not execute this instruction, so we can't hoist it.

  // If the instruction is in the header block for the loop (which is very
  // common), it is always guaranteed to dominate the exit blocks.  Since this
  // is a common case, and can save some work, check it now.
  if (Inst.getParent() == CurLoop->getHeader())
    return true;

  // Get the exit blocks for the current loop.
  SmallVector<BasicBlock*, 8> ExitBlocks;
  CurLoop->getExitBlocks(ExitBlocks);

  // Verify that the block dominates each of the exit blocks of the loop.
  for (unsigned i = 0, e = ExitBlocks.size(); i != e; ++i)
    if (!DT->dominates(Inst.getParent(), ExitBlocks[i]))
      return false;

  // As a degenerate case, if the loop is statically infinite then we haven't
  // proven anything since there are no exit blocks.
  if (ExitBlocks.empty())
    return false;

  return true;
}

namespace {
  class LoopPromoter : public LoadAndStorePromoter {
    Value *SomePtr;  // Designated pointer to store to.
    SmallPtrSet<Value*, 4> &PointerMustAliases;
    SmallVectorImpl<BasicBlock*> &LoopExitBlocks;
    SmallVectorImpl<Instruction*> &LoopInsertPts;
    AliasSetTracker &AST;
    DebugLoc DL;
    int Alignment;
    MDNode *TBAATag;
  public:
    LoopPromoter(Value *SP,
                 const SmallVectorImpl<Instruction*> &Insts, SSAUpdater &S,
                 SmallPtrSet<Value*, 4> &PMA,
                 SmallVectorImpl<BasicBlock*> &LEB,
                 SmallVectorImpl<Instruction*> &LIP,
                 AliasSetTracker &ast, DebugLoc dl, int alignment,
                 MDNode *TBAATag)
      : LoadAndStorePromoter(Insts, S), SomePtr(SP),
        PointerMustAliases(PMA), LoopExitBlocks(LEB), LoopInsertPts(LIP),
        AST(ast), DL(dl), Alignment(alignment), TBAATag(TBAATag) {}

    virtual bool isInstInList(Instruction *I,
                              const SmallVectorImpl<Instruction*> &) const {
      Value *Ptr;
      if (LoadInst *LI = dyn_cast<LoadInst>(I))
        Ptr = LI->getOperand(0);
      else
        Ptr = cast<StoreInst>(I)->getPointerOperand();
      return PointerMustAliases.count(Ptr);
    }

    virtual void doExtraRewritesBeforeFinalDeletion() const {
      // Insert stores after in the loop exit blocks.  Each exit block gets a
      // store of the live-out values that feed them.  Since we've already told
      // the SSA updater about the defs in the loop and the preheader
      // definition, it is all set and we can start using it.
      for (unsigned i = 0, e = LoopExitBlocks.size(); i != e; ++i) {
        BasicBlock *ExitBlock = LoopExitBlocks[i];
        Value *LiveInValue = SSA.GetValueInMiddleOfBlock(ExitBlock);
        Instruction *InsertPos = LoopInsertPts[i];
        StoreInst *NewSI = new StoreInst(LiveInValue, SomePtr, InsertPos);
        NewSI->setAlignment(Alignment);
        NewSI->setDebugLoc(DL);
        if (TBAATag) NewSI->setMetadata(LLVMContext::MD_tbaa, TBAATag);
      }
    }

    virtual void replaceLoadWithValue(LoadInst *LI, Value *V) const {
      // Update alias analysis.
      AST.copyValue(LI, V);
    }
    virtual void instructionDeleted(Instruction *I) const {
      AST.deleteValue(I);
    }
  };
} // end anon namespace

/// PromoteAliasSet - Try to promote memory values to scalars by sinking
/// stores out of the loop and moving loads to before the loop.  We do this by
/// looping over the stores in the loop, looking for stores to Must pointers
/// which are loop invariant.
///
void SLICM::PromoteAliasSet(AliasSet &AS,
                           SmallVectorImpl<BasicBlock*> &ExitBlocks,
                           SmallVectorImpl<Instruction*> &InsertPts) {
  // We can promote this alias set if it has a store, if it is a "Must" alias
  // set, if the pointer is loop invariant, and if we are not eliminating any
  // volatile loads or stores.
  if (AS.isForwardingAliasSet() || !AS.isMod() || !AS.isMustAlias() ||
      AS.isVolatile() || !CurLoop->isLoopInvariant(AS.begin()->getValue()))
    return;

  assert(!AS.empty() &&
         "Must alias set should have at least one pointer element in it!");
  Value *SomePtr = AS.begin()->getValue();

  // It isn't safe to promote a load/store from the loop if the load/store is
  // conditional.  For example, turning:
  //
  //    for () { if (c) *P += 1; }
  //
  // into:
  //
  //    tmp = *P;  for () { if (c) tmp +=1; } *P = tmp;
  //
  // is not safe, because *P may only be valid to access if 'c' is true.
  //
  // It is safe to promote P if all uses are direct load/stores and if at
  // least one is guaranteed to be executed.
  bool GuaranteedToExecute = false;

  SmallVector<Instruction*, 64> LoopUses;
  SmallPtrSet<Value*, 4> PointerMustAliases;

  // We start with an alignment of one and try to find instructions that allow
  // us to prove better alignment.
  unsigned Alignment = 1;
  MDNode *TBAATag = 0;

  // Check that all of the pointers in the alias set have the same type.  We
  // cannot (yet) promote a memory location that is loaded and stored in
  // different sizes.  While we are at it, collect alignment and TBAA info.
  for (AliasSet::iterator ASI = AS.begin(), E = AS.end(); ASI != E; ++ASI) {
    Value *ASIV = ASI->getValue();
    PointerMustAliases.insert(ASIV);

    // Check that all of the pointers in the alias set have the same type.  We
    // cannot (yet) promote a memory location that is loaded and stored in
    // different sizes.
    if (SomePtr->getType() != ASIV->getType())
      return;

    for (Value::use_iterator UI = ASIV->use_begin(), UE = ASIV->use_end();
         UI != UE; ++UI) {
      // Ignore instructions that are outside the loop.
      Instruction *Use = dyn_cast<Instruction>(*UI);
      if (!Use || !CurLoop->contains(Use))
        continue;

      // If there is an non-load/store instruction in the loop, we can't promote
      // it.
      if (LoadInst *load = dyn_cast<LoadInst>(Use)) {
        assert(!load->isVolatile() && "AST broken");
        if (!load->isSimple())
          return;
      } else if (StoreInst *store = dyn_cast<StoreInst>(Use)) {
        // Stores *of* the pointer are not interesting, only stores *to* the
        // pointer.
        if (Use->getOperand(1) != ASIV)
          continue;
        assert(!store->isVolatile() && "AST broken");
        if (!store->isSimple())
          return;

        // Note that we only check GuaranteedToExecute inside the store case
        // so that we do not introduce stores where they did not exist before
        // (which would break the LLVM concurrency model).

        // If the alignment of this instruction allows us to specify a more
        // restrictive (and performant) alignment and if we are sure this
        // instruction will be executed, update the alignment.
        // Larger is better, with the exception of 0 being the best alignment.
        unsigned InstAlignment = store->getAlignment();
        if ((InstAlignment > Alignment || InstAlignment == 0) && Alignment != 0)
          if (isGuaranteedToExecute(*Use)) {
            GuaranteedToExecute = true;
            Alignment = InstAlignment;
          }

        if (!GuaranteedToExecute)
          GuaranteedToExecute = isGuaranteedToExecute(*Use);

      } else
        return; // Not a load or store.

      // Merge the TBAA tags.
      if (LoopUses.empty()) {
        // On the first load/store, just take its TBAA tag.
        TBAATag = Use->getMetadata(LLVMContext::MD_tbaa);
      } else if (TBAATag) {
        TBAATag = MDNode::getMostGenericTBAA(TBAATag,
                                       Use->getMetadata(LLVMContext::MD_tbaa));
      }
      
      LoopUses.push_back(Use);
    }
  }

  // If there isn't a guaranteed-to-execute instruction, we can't promote.
  if (!GuaranteedToExecute)
    return;

  // Otherwise, this is safe to promote, lets do it!
  DEBUG(dbgs() << "SLICM: Promoting value stored to in loop: " <<*SomePtr<<'\n');
  Changed = true;
  ++NumPromoted;

  // Grab a debug location for the inserted loads/stores; given that the
  // inserted loads/stores have little relation to the original loads/stores,
  // this code just arbitrarily picks a location from one, since any debug
  // location is better than none.
  DebugLoc DL = LoopUses[0]->getDebugLoc();

  // Figure out the loop exits and their insertion points, if this is the
  // first promotion.
  if (ExitBlocks.empty()) {
    CurLoop->getUniqueExitBlocks(ExitBlocks);
    InsertPts.resize(ExitBlocks.size());
    for (unsigned i = 0, e = ExitBlocks.size(); i != e; ++i)
      InsertPts[i] = ExitBlocks[i]->getFirstInsertionPt();
  }

  // We use the SSAUpdater interface to insert phi nodes as required.
  SmallVector<PHINode*, 16> NewPHIs;
  SSAUpdater SSA(&NewPHIs);
  LoopPromoter Promoter(SomePtr, LoopUses, SSA, PointerMustAliases, ExitBlocks,
                        InsertPts, *CurAST, DL, Alignment, TBAATag);

  // Set up the preheader to have a definition of the value.  It is the live-out
  // value from the preheader that uses in the loop will use.
  LoadInst *PreheaderLoad =
    new LoadInst(SomePtr, SomePtr->getName()+".promoted",
                 Preheader->getTerminator());
  PreheaderLoad->setAlignment(Alignment);
  PreheaderLoad->setDebugLoc(DL);
  if (TBAATag) PreheaderLoad->setMetadata(LLVMContext::MD_tbaa, TBAATag);
  SSA.AddAvailableValue(Preheader, PreheaderLoad);

  // Rewrite all the loads in the loop and remember all the definitions from
  // stores in the loop.
  Promoter.run(LoopUses);

  // If the SSAUpdater didn't use the load in the preheader, just zap it now.
  if (PreheaderLoad->use_empty())
    PreheaderLoad->eraseFromParent();
}


/// cloneBasicBlockAnalysis - Simple Analysis hook. Clone alias set info.
void SLICM::cloneBasicBlockAnalysis(BasicBlock *From, BasicBlock *To, Loop *L) {
  AliasSetTracker *AST = LoopToAliasSetMap.lookup(L);
  if (!AST)
    return;

  AST->copyValue(From, To);
}

/// deleteAnalysisValue - Simple Analysis hook. Delete value V from alias
/// set.
void SLICM::deleteAnalysisValue(Value *V, Loop *L) {
  AliasSetTracker *AST = LoopToAliasSetMap.lookup(L);
  if (!AST)
    return;

  AST->deleteValue(V);
}
