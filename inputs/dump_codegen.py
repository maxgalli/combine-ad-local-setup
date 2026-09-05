# Build the NLL through Combine's own entry point (the only path that honours
# Combine::nllBackend()) and dump the generated C++.
import sys, ROOT

wsfile = sys.argv[1]
backend = sys.argv[2] if len(sys.argv) > 2 else "codegen"
outname = sys.argv[3] if len(sys.argv) > 3 else "codegen"

ROOT.gInterpreter.Declare('#include <HiggsAnalysis/CombinedLimit/interface/Combine.h>')
ROOT.gInterpreter.Declare('#include <HiggsAnalysis/CombinedLimit/interface/CombineUtils.h>')
ROOT.Combine.nllBackend_ = backend

f = ROOT.TFile.Open(wsfile)
w = f.Get("w")
pdf, data = w["model_s"], w["data_obs"]
nuis = w.set("nuisances")

ROOT.RooMsgService.instance().setGlobalKillBelow(ROOT.RooFit.ERROR)
nll = ROOT.CombineUtils.combineCreateNLL(pdf, data, nuis, False, False)
print("pdf class :", pdf.IsA().GetName())
print("NLL value :", nll.getVal())

if backend.startswith("codegen"):
    ROOT.RooFit.Experimental.writeCodegenDebugMacro(nll, outname)
    print("debug macro written:", outname)
