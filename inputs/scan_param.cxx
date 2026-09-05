// Scan the NLL vs an arbitrary parameter, through Combine's own NLL entry point.
//   usage: scan_param <ws.root> <backend> <paramName>
#include <TFile.h>
#include <RooWorkspace.h>
#include <RooAbsPdf.h>
#include <RooAbsData.h>
#include <RooArgSet.h>
#include <RooRealVar.h>
#include <RooMsgService.h>
#include <iostream>
#include <iomanip>
#include <memory>
#include <string>
#include "COMBINE_INC/interface/Combine.h"
#include "COMBINE_INC/interface/CombineUtils.h"

int main(int argc, char **argv) {
  std::string ws = argv[1], backend = argv[2], pname = argv[3];
  RooMsgService::instance().setGlobalKillBelow(RooFit::ERROR);
  Combine::nllBackend() = backend;
  std::unique_ptr<TFile> f{TFile::Open(ws.c_str())};
  auto *w = dynamic_cast<RooWorkspace *>(f->Get("w"));
  auto nll = combineCreateNLL(*w->pdf("model_s"), *w->data("data_obs"), w->set("nuisances"), false, false);
  auto *p = w->var(pname.c_str());
  if (!p) { std::cerr << "no such parameter: " << pname << "\n"; return 1; }
  for (double v : {-2.0, -1.0, -0.5, 0.0, 0.5, 1.0, 2.0}) {
    p->setVal(v);
    std::cout << pname << "=" << std::setw(5) << v
              << "  NLL=" << std::setprecision(17) << nll->getVal() << "\n";
  }
  return 0;
}
