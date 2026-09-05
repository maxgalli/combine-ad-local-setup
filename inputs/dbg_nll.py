import ROOT
ROOT.gInterpreter.Declare('#include <HiggsAnalysis/CombinedLimit/interface/Combine.h>')
ROOT.gInterpreter.Declare('#include <HiggsAnalysis/CombinedLimit/interface/CombineUtils.h>')
ROOT.RooMsgService.instance().setGlobalKillBelow(ROOT.RooFit.ERROR)
f = ROOT.TFile.Open("ws5.root")
w = f.Get("w")
pdf, data, nuis = w["model_s"], w["data_obs"], w.set("nuisances")
for be in ["combine", "cpu", "codegen"]:
    ROOT.Combine.nllBackend_ = be
    nll = ROOT.CombineUtils.combineCreateNLL(pdf, data, nuis, False, False)
    if nll is None:
        print("  backend=%-8s -> NULL" % be)
        continue
    print("  backend=%-8s class=%-22s val=%.10f" % (be, nll.IsA().GetName(), nll.getVal()))
