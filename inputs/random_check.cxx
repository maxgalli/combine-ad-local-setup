// Randomise every nuisance and compare NLL between two backends.
//   usage: random_check <ws.root> <ntrials>
#include <TFile.h>
#include <TRandom3.h>
#include <RooWorkspace.h>
#include <RooAbsPdf.h>
#include <RooAbsData.h>
#include <RooArgSet.h>
#include <RooRealVar.h>
#include <RooMsgService.h>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <memory>
#include <vector>
#include "COMBINE_INC/interface/Combine.h"
#include "COMBINE_INC/interface/CombineUtils.h"

int main(int argc, char **argv) {
  std::string ws = argv[1];
  int ntrials = argc > 2 ? std::atoi(argv[2]) : 20;
  RooMsgService::instance().setGlobalKillBelow(RooFit::ERROR);

  std::unique_ptr<TFile> f{TFile::Open(ws.c_str())};
  auto *w = dynamic_cast<RooWorkspace *>(f->Get("w"));
  auto *pdf = w->pdf("model_s");
  auto *data = w->data("data_obs");
  auto const *nuis = w->set("nuisances");

  Combine::nllBackend() = "cpu";
  auto nllCpu = combineCreateNLL(*pdf, *data, nuis, false, false);
  Combine::nllBackend() = "codegen";
  auto nllGen = combineCreateNLL(*pdf, *data, nuis, false, false);

  std::vector<RooRealVar *> pars;
  for (auto *a : *nuis)
    if (auto *v = dynamic_cast<RooRealVar *>(a))
      if (!v->isConstant()) pars.push_back(v);
  if (auto *r = w->var("r")) pars.push_back(r);

  TRandom3 rng(1234);
  double worst = 0.;
  for (int t = 0; t < ntrials; ++t) {
    for (auto *v : pars) {
      double lo = v->getMin(), hi = v->getMax();
      double x = v->getVal() + rng.Gaus(0., 1.);
      v->setVal(std::min(std::max(x, lo), hi));
    }
    double a = nllCpu->getVal();
    double b = nllGen->getVal();
    double rel = std::abs(a - b) / std::max(1.0, std::abs(a));
    worst = std::max(worst, rel);
    if (t < 3 || rel > 1e-12)
      std::cout << "  trial " << t << "  cpu=" << std::setprecision(17) << a
                << "  codegen=" << b << "  rel=" << std::scientific << rel << std::defaultfloat << "\n";
  }
  std::cout << "  parameters varied: " << pars.size() << "\n";
  std::cout << "  worst relative difference over " << ntrials << " trials: "
            << std::scientific << worst << "\n";
  return worst < 1e-10 ? 0 : 1;
}
