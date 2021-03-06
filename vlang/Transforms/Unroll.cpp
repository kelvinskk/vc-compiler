#include "llvm/ADT/Triple.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/Analysis/CallGraphSCCPass.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/RegionPass.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/Bitcode/BitcodeWriterPass.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/LegacyPassNameParser.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/InitializePasses.h"
#include "llvm/LinkAllIR.h"
#include "llvm/LinkAllPasses.h"
#include "llvm/MC/SubtargetFeature.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Host.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/PluginLoader.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/SystemUtils.h"
#include "llvm/Support/TargetRegistry.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/YAMLTraits.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Coroutines.h"
#include "llvm/Transforms/IPO/AlwaysInliner.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/Demangle/Demangle.h"
#include <algorithm>
#include <memory>
using namespace llvm;

StringRef getSimpleName(StringRef SymbolName) {
  int Status;
  StringRef DemangledSymbol;

  DemangledSymbol =
        itaniumDemangle(SymbolName.data(), nullptr, nullptr, &Status);

  if (Status != 0 || DemangledSymbol.empty())
    return SymbolName;
  else {
    StringRef Name = DemangledSymbol.split('(').first;
    size_t len0 = Name.rfind(' ') + 1;
    Name = Name.drop_front(len0);
    size_t len1 = Name.rfind(':') + 1;
    Name = Name.drop_front(len1);
    return Name;
  }
}

static void AddOptimizationPasses(legacy::PassManagerBase &MPM,
                                  legacy::FunctionPassManager &FPM,
                                  unsigned OptLevel,
                                  unsigned SizeLevel) {
    FPM.add(createVerifierPass()); // Verify that input is correct

  PassManagerBuilder Builder;
  Builder.OptLevel = OptLevel;
  Builder.SizeLevel = SizeLevel;

    Builder.Inliner = createFunctionInliningPass(OptLevel, SizeLevel, false);

  Builder.DisableUnitAtATime = false;

  Builder.DisableUnrollLoops = false;

    Builder.LoopVectorize = true;

  Builder.SLPVectorize = true;


  Builder.populateFunctionPassManager(FPM);
  Builder.populateModulePassManager(MPM);

//  Builder.populateLTOPassManager(PM);
}




ModulePass* createDepandentGraphPass ();
FunctionPass* createRemoveAttrsPass ();
FunctionPass* createEliminateCFGPass();
ModulePass* createLocalizationPass ();
LoopPass* createAddUnrollFullPass ();
ModulePass* createVerifyUnrollPass ();
BasicBlockPass* createInstFormatPass ();

void VCPass(llvm::Module &M){
  // Initialize passes
  PassRegistry &Registry = *PassRegistry::getPassRegistry();
  initializeCore(Registry);
  initializeCoroutines(Registry);
  initializeScalarOpts(Registry);
  initializeObjCARCOpts(Registry);
  initializeVectorization(Registry);
  initializeIPO(Registry);
  initializeAnalysis(Registry);
  initializeTransformUtils(Registry);
  initializeInstCombine(Registry);
  initializeInstrumentation(Registry);
  initializeTarget(Registry);

  legacy::PassManager PrePasses;
  legacy::PassManager Passes;
  legacy::FunctionPassManager FPasses(&M);



  PrePasses.add(createRemoveAttrsPass());
  PrePasses.add(createAddUnrollFullPass());
  auto PreserveMain = [=](const GlobalValue &GV) { return getSimpleName(GV.getName()) == "vc_main";};
  PrePasses.add(createInternalizePass(PreserveMain));

  PrePasses.run(M);




  FPasses.add(createTargetTransformInfoWrapperPass(TargetIRAnalysis()));
  
  AddOptimizationPasses(Passes, FPasses, 2, 0);


  FPasses.doInitialization();
  for (Function &F : M)
    FPasses.run(F);
  FPasses.doFinalization();


  Passes.add(createDepandentGraphPass());
  Passes.add(createLocalizationPass());

  Passes.add(createPromoteMemoryToRegisterPass());
  Passes.add(createLowerSwitchPass());
  Passes.add(createUnifyFunctionExitNodesPass());

  Passes.add(createEliminateCFGPass());

  Passes.add(createVerifierPass());
  Passes.add(createVerifyUnrollPass());
  Passes.add(createInstFormatPass());

  Passes.run(M);

}
