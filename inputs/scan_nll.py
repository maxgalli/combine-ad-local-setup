# Scan the NLL vs r under two backends, using Combine's own NLL construction.
import sys, ROOT

wsfile, backend = sys.argv[1], sys.argv[2]

ROOT.gInterpreter.Declare('#include <HiggsAnalysis/CombinedLimit/interface/Combine.h>')
ROOT.gInterpreter.Declare('#include <HiggsAnalysis/CombinedLimit/interface/CombineMathFuncs.h>')
ROOT.Combine.nllBackend_ = backend

f = ROOT.TFile.Open(wsfile)
w = f.Get("w")
pdf, data = w["model_s"], w["data_obs"]
gobs, nuis = w.set("globalObservables"), w.set("nuisances")

ROOT.RooMsgService.instance().setGlobalKillBelow(ROOT.RooFit.ERROR)
nll = pdf.createNLL(data, Constrain=nuis, GlobalObservables=gobs, Offset="initial")

r = w["r"]
print(f"backend={backend}  class={nll.ClassName()}")
base = None
for val in [0.0, 0.5, 1.0, 2.0, 2.82, 5.0]:
    r.setVal(val)
    v = nll.getVal()
    if base is None:
        base = v
    print(f"  r={val:5.2f}   NLL={v:.8f}   dNLL={v-base:+.8f}")
