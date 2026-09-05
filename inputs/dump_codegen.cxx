// Build the NLL via Combine's own entry point and dump the generated C++.
//   usage: dump_codegen <ws.root> <backend> [macroname]
#include <TFile.h>
#include <RooWorkspace.h>
#include <RooAbsPdf.h>
#include <RooAbsData.h>
#include <RooArgSet.h>
#include <RooRealVar.h>
#include <RooMsgService.h>
#include <RooGlobalFunc.h>

#include "../src/.worktrees/pr1252/interface/Combine.h"
#include "../src/.worktrees/pr1252/interface/CombineUtils.h"

#include <iostream>
#include <memory>
#include <string>

int main(int argc, char **argv) {
  std::string ws = argv[1];
  std::string backend = argc > 2 ? argv[2] : "codegen";
  std::string macro = argc > 3 ? argv[3] : "gen";

  RooMsgService::instance().setGlobalKillBelow(RooFit::ERROR);
  Combine::nllBackend() = backend;

  std::unique_ptr<TFile> f{TFile::Open(ws.c_str())};
  auto *w = dynamic_cast<RooWorkspace *>(f->Get("w"));
  auto *pdf = w->pdf("model_s");
  auto *data = w->data("data_obs");
  auto const *nuis = w->set("nuisances");

  std::cout << "pdf class : " << pdf->ClassName() << "\n";
  auto nll = combineCreateNLL(*pdf, *data, nuis, /*offset=*/false, /*warn=*/false);
  std::cout << "nll class : " << nll->ClassName() << "\n";

  auto *r = w->var("r");
  for (double v : {0.0, 0.5, 1.0, 1.337, 2.0}) {
    r->setVal(v);
    std::cout << "  r=" << v << "  NLL=" << std::setprecision(12) << nll->getVal() << "\n";
  }

  if (backend.rfind("codegen", 0) == 0) {
    RooFit::Experimental::writeCodegenDebugMacro(*nll, macro);
    std::cout << "wrote macro: " << macro << "\n";
  }
  return 0;
}
