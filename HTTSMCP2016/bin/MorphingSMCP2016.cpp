#include <string>
#include <map>
#include <set>
#include <iostream>
#include <utility>
#include <vector>
#include <cstdlib>
#include "boost/algorithm/string/predicate.hpp"
#include "boost/program_options.hpp"
#include "boost/lexical_cast.hpp"
#include "boost/regex.hpp"
#include "CombineHarvester/CombineTools/interface/CombineHarvester.h"
#include "CombineHarvester/CombineTools/interface/Observation.h"
#include "CombineHarvester/CombineTools/interface/Process.h"
#include "CombineHarvester/CombineTools/interface/Utilities.h"
#include "CombineHarvester/CombineTools/interface/CardWriter.h"
#include "CombineHarvester/CombineTools/interface/Systematics.h"
#include "CombineHarvester/CombineTools/interface/BinByBin.h"
#include "CombineHarvester/CombineTools/interface/Algorithm.h"
#include "CombineHarvester/CombineTools/interface/AutoRebin.h"
#include "CombineHarvester/CombinePdfs/interface/MorphFunctions.h"
#include "CombineHarvester/HTTSMCP2016/interface/HttSystematics_SMRun2.h"
#include "RooWorkspace.h"
#include "RooRealVar.h"
#include "TH2.h"
#include "TF1.h"

using namespace std;
using boost::starts_with;
namespace po = boost::program_options;

template <typename T>
void To1Bin(T* proc)
{
    std::unique_ptr<TH1> originalHist = proc->ClonedScaledShape();
    TH1F *hist = new TH1F("hist","hist",1,0,1);
    double err = 0;
    double rate =
    originalHist->IntegralAndError(0, originalHist->GetNbinsX() + 1, err);
    hist->SetDirectory(0);
    hist->SetBinContent(1, rate);
    hist->SetBinError(1, err);
    proc->set_shape(*hist, true);  // True means adjust the process rate to the
    // integral of the hist
}

bool BinIsControlRegion(ch::Object const* obj)
{
    return (boost::regex_search(obj->bin(),boost::regex{"_cr$"}) );
}

// Useful to have the inverse sometimes too
bool BinIsNotControlRegion(ch::Object const* obj)
{
    return !BinIsControlRegion(obj);
}


int main(int argc, char** argv) {

    string output_folder = "sm_run2";
    string input_folder_em="Imperial/CP/";
    string input_folder_et="Imperial/CP/";
    string input_folder_mt="Imperial/CP/";
    string input_folder_tt="Imperial/CP/";
    string input_folder_mm="USCMS/";
    string input_folder_ttbar="USCMS/";
    string only_init="";
    string postfix="";
    int control_region = 0;
    bool ttbar_fit = false;
    bool real_data = false;
    bool no_shape_systs = false;
    bool do_embedding = false;
    bool auto_rebin = false;
    bool no_jec_split = false;    
    bool do_jetfakes = false;
    po::variables_map vm;
    po::options_description config("configuration");
    config.add_options()
    ("input_folder_em", po::value<string>(&input_folder_em)->default_value("Imperial/CP"))
    ("input_folder_et", po::value<string>(&input_folder_et)->default_value("Imperial/CP"))
    ("input_folder_mt", po::value<string>(&input_folder_mt)->default_value("Imperial/CP"))
    ("input_folder_tt", po::value<string>(&input_folder_tt)->default_value("Imperial/CP"))
    ("input_folder_mm", po::value<string>(&input_folder_mm)->default_value("USCMS"))
    ("input_folder_ttbar", po::value<string>(&input_folder_ttbar)->default_value("USCMS"))
    ("only_init", po::value<string>(&only_init)->default_value(""))
    ("real_data", po::value<bool>(&real_data)->default_value(real_data))
    ("postfix", po::value<string>(&postfix)->default_value(postfix))
    ("output_folder", po::value<string>(&output_folder)->default_value("sm_run2"))
    ("control_region", po::value<int>(&control_region)->default_value(1))
    ("no_shape_systs", po::value<bool>(&no_shape_systs)->default_value(no_shape_systs))
    ("do_embedding", po::value<bool>(&do_embedding)->default_value(false))
    ("do_jetfakes", po::value<bool>(&do_jetfakes)->default_value(false))
    ("auto_rebin", po::value<bool>(&auto_rebin)->default_value(false))
    ("no_jec_split", po::value<bool>(&no_jec_split)->default_value(true))    
    ("ttbar_fit", po::value<bool>(&ttbar_fit)->default_value(true));

    po::store(po::command_line_parser(argc, argv).options(config).run(), vm);
    po::notify(vm);
   
    if(do_jetfakes) control_region = 0; 
    
    typedef vector<string> VString;
    typedef vector<pair<int, string>> Categories;
    //! [part1]
    // First define the location of the "auxiliaries" directory where we can
    // source the input files containing the datacard shapes
    //    string aux_shapes = string(getenv("CMSSW_BASE")) + "/src/CombineHarvester/CombineTools/bin/AllROOT_20fb/";
    std::map<string, string> input_dir;
    input_dir["em"]  = string(getenv("CMSSW_BASE")) + "/src/CombineHarvester/HTTSMCP2016/shapes/"+input_folder_em+"/";
    input_dir["mt"]  = string(getenv("CMSSW_BASE")) + "/src/CombineHarvester/HTTSMCP2016/shapes/"+input_folder_mt+"/";
    input_dir["et"]  = string(getenv("CMSSW_BASE")) + "/src/CombineHarvester/HTTSMCP2016/shapes/"+input_folder_et+"/";
    input_dir["tt"]  = string(getenv("CMSSW_BASE")) + "/src/CombineHarvester/HTTSMCP2016/shapes/"+input_folder_tt+"/";
    input_dir["ttbar"]  = string(getenv("CMSSW_BASE")) + "/src/CombineHarvester/HTTSMCP2016/shapes/"+input_folder_em+"/";    
    
    
    VString chns = {"mt","et","tt","em"};
    if (ttbar_fit) chns.push_back("ttbar");
    
    map<string, VString> bkg_procs;
    bkg_procs["et"] = {"ZTT", "QCD", "ZL", "ZJ","TTT","TTJ", "VVT", "VVJ", "EWKZ", "W"};
    bkg_procs["mt"] = {"ZTT", "QCD", "ZL", "ZJ","TTT","TTJ", "VVT", "VVJ", "EWKZ", "W"};
    bkg_procs["tt"] = {"ZTT", "W", "QCD", "ZL", "ZJ","TTT","TTJ",  "VVT","VVJ", "EWKZ"};
    bkg_procs["em"] = {"ZTT","W", "QCD", "ZLL", "TT", "VV", "EWKZ", "ggH_hww125", "qqH_hww125"};
    bkg_procs["ttbar"] = {"ZTT", "W", "QCD", "ZLL", "TT", "VV", "EWKZ"};
    
    if(do_embedding){
      bkg_procs["et"] = {"EmbedZTT", "QCD", "ZL", "ZJ","TTT","TTJ", "VVT", "VVJ", "W"};
      bkg_procs["mt"] = {"EmbedZTT", "QCD", "ZL", "ZJ","TTT","TTJ",  "VVT", "VVJ", "W"};
      bkg_procs["tt"] = {"EmbedZTT", "W", "QCD", "ZL", "ZJ","TTT","TTJ",  "VVT","VVJ"};
      // Not use embedding for em channel currently
      //bkg_procs["em"] = {"EmbedZTT","W", "QCD", "ZLL", "TT", "VV", "ggH_hww125", "qqH_hww125"};
      //bkg_procs["ttbar"] = {"EmbedZTT", "W", "QCD", "ZLL", "TT", "VV"};
    }

    if(do_jetfakes){
      bkg_procs["et"] = {"ZTT", "ZL", "TTT", "VVT", "EWKZ", "jetFakes"};
      bkg_procs["mt"] = {"ZTT", "ZL", "TTT", "VVT", "EWKZ", "jetFakes"};
      bkg_procs["tt"] = {"ZTT", "ZL", "TTT", "VVT", "EWKZ", "jetFakes"};

      if(do_embedding){
        bkg_procs["et"] = {"EmbedZTT", "ZL", "TTT", "VVT", "jetFakes"};
        bkg_procs["mt"] = {"EmbedZTT", "ZL", "TTT", "VVT", "jetFakes"};
        bkg_procs["tt"] = {"EmbedZTT", "ZL", "TTT", "VVT", "jetFakes"};
      }

    }

    ch::CombineHarvester cb;
    
    map<string,Categories> cats;
    cats["et"] = {
        {1, "et_0jet"},
        {2, "et_boosted"}
    };
    
    cats["mt"] = {
        {1, "mt_0jet"},
        {2, "mt_boosted"}
    }; 
    cats["em"] = {
        {1, "em_0jet"},
        {2, "em_boosted"}
    };
    
    cats["tt"] = {
        {1, "tt_0jet"},
        {2, "tt_boosted"}
    };
    
    cats["ttbar"] = {
        {1, "em_ttbar"}
    };
    
    map<string,Categories> cats_cp;
    
    cats_cp["em"] = {
        {3, "em_dijet_lowboost"},
        {4, "em_dijet_boosted"} 
    };
    
    cats_cp["et"] = {
        {3, "et_dijet_lowboost"},
        {4, "et_dijet_boosted"}       
    };
    
    cats_cp["mt"] = {
        {3, "mt_dijet_lowboost"},
        {4, "mt_dijet_boosted"}
    };    
    
    cats_cp["tt"] = {
        {3, "tt_dijet_lowboost"},
        {4, "tt_dijet_boosted"}
    };    
    
    if (control_region > 0){
        // for each channel use the categories >= 10 for the control regions
        // the control regions are ordered in triples (10,11,12),(13,14,15)...
        for (auto chn : chns){
            if (ch::contains({"mt", "et"}, chn)) {
                    
                Categories queue;
                int binid = 10;
                for (auto cat:cats[chn]){
                    queue.push_back(make_pair(binid,cat.second+"_wjets_cr"));
                    queue.push_back(make_pair(binid+1,cat.second+"_qcd_cr"));
                    queue.push_back(make_pair(binid+2,cat.second+"_wjets_ss_cr"));
                    binid+=3;
                }
                queue.push_back(make_pair(binid,chn+"_dijet_lowboost_wjets_cr"));
                queue.push_back(make_pair(binid+1,chn+"_dijet_lowboost_qcd_cr"));
                queue.push_back(make_pair(binid+2,chn+"_dijet_lowboost_wjets_ss_cr"));
                queue.push_back(make_pair(binid+4,chn+"_dijet_boosted_qcd_cr")); 
                
                cats[chn].insert(cats[chn].end(),queue.begin(),queue.end());
            }
            // Add tautau QCD CRs
            if (ch::contains({"tt"}, chn)) {
                    
                Categories queue;
                int binid = 10;
                
                queue.push_back(make_pair(binid,chn+"_0jet_qcd_cr"));
                queue.push_back(make_pair(binid+1,chn+"_boosted_qcd_cr"));
                queue.push_back(make_pair(binid+2,chn+"_dijet_lowboost_qcd_cr"));
                queue.push_back(make_pair(binid+3,chn+"_dijet_boosted_qcd_cr"));    
                
                cats[chn].insert(cats[chn].end(),queue.begin(),queue.end());
            }
        }
    } // end CR et mt > 0

    
    map<string, VString> sig_procs;
    sig_procs["ggH"] = {"ggH_htt"};
    sig_procs["qqH"] = {"qqHsm_htt125","WH_htt125","ZH_htt125"}; // using JHU samples for qqH
    sig_procs["qqH_BSM"] = {"qqHmm_htt","qqHps_htt"};
    sig_procs["ggHCP"] = {"ggHsm_htt", "ggHps_htt", "ggHmm_htt"};
    vector<string> masses = {"125"};    
    
    using ch::syst::bin_id;
    
    //! [part2]
    for (auto chn : chns) {
        cb.AddObservations({"*"}, {"htt"}, {"13TeV"}, {chn}, cats[chn]);
        cb.AddObservations({"*"}, {"htt"}, {"13TeV"}, {chn}, cats_cp[chn]);

        cb.AddProcesses(   {"*"}, {"htt"}, {"13TeV"}, {chn}, bkg_procs[chn], cats[chn], false);
        cb.AddProcesses(   {"*"}, {"htt"}, {"13TeV"}, {chn}, bkg_procs[chn], cats_cp[chn], false);

        if(chn == "em" || chn == "et" || chn == "mt" || chn == "tt"){
          cb.AddProcesses({"*"},   {"htt"}, {"13TeV"}, {chn}, sig_procs["qqH"], cats[chn], false); // SM VBF/VH are added as backgrounds
          cb.AddProcesses({"*"},   {"htt"}, {"13TeV"}, {chn}, sig_procs["qqH"], cats_cp[chn], false);
          
          cb.AddProcesses(masses,   {"htt"}, {"13TeV"}, {chn}, sig_procs["qqH_BSM"], cats[chn], true); // Non-SM VBF/VH are added as signal
          cb.AddProcesses(masses,   {"htt"}, {"13TeV"}, {chn}, sig_procs["qqH_BSM"], cats_cp[chn], true);
           
          cb.AddProcesses(masses,   {"htt"}, {"13TeV"}, {chn}, sig_procs["ggH"], cats[chn], true);
          cb.AddProcesses(masses,   {"htt"}, {"13TeV"}, {chn}, sig_procs["ggHCP"], cats_cp[chn], true);
        }
    }
    
    //! [part4]
    
    
    if (control_region > 0){
        // Since we now account for QCD in the high mT region we only
        // need to filter signal processes
        cb.FilterAll([](ch::Object const* obj) {
            return (BinIsControlRegion(obj) && obj->signal());
        });
    }
    
    
    ch::AddSMRun2Systematics(cb, control_region, ttbar_fit, no_jec_split);
    
    if(no_shape_systs){
      cb.FilterSysts([&](ch::Systematic *s){
        return s->type().find("shape") != std::string::npos;
      });
    }
    

    if (! only_init.empty()) {
        std::cout << "Write datacards (without shapes) to directory \"" << only_init << "\" and quit." << std::endl;
        ch::CardWriter tmpWriter("$TAG/$ANALYSIS_$ERA_$CHANNEL_$BINID_$MASS.txt", "$TAG/dummy.root");
        tmpWriter.WriteCards(only_init, cb);
        
        return 0;
    }
            
    //! [part7]
    for (string chn : cb.channel_set()){
        string channel = chn;
        if(chn == "ttbar") channel = "em"; 
        cb.cp().channel({chn}).backgrounds().ExtractShapes(
                                                           input_dir[chn] + "htt_"+channel+".inputs-sm-13TeV"+postfix+".root",
                                                           "$BIN/$PROCESS",
                                                           "$BIN/$PROCESS_$SYSTEMATIC");
        if(chn == "em" || chn == "et" || chn == "mt" || chn == "tt"){
          cb.cp().channel({chn}).process(sig_procs["qqH_BSM"]).ExtractShapes(
                                                                  input_dir[chn] + "htt_"+chn+".inputs-sm-13TeV"+postfix+".root",
                                                                  "$BIN/$PROCESS$MASS",
                                                                  "$BIN/$PROCESS$MASS_$SYSTEMATIC");
          cb.cp().channel({chn}).process(sig_procs["ggH"]).ExtractShapes(
                                                                  input_dir[chn] + "htt_"+chn+".inputs-sm-13TeV"+postfix+".root",
                                                                  "$BIN/$PROCESS$MASS",
                                                                  "$BIN/$PROCESS$MASS_$SYSTEMATIC");
          cb.cp().channel({chn}).process(sig_procs["ggHCP"]).ExtractShapes(
                                                                  input_dir[chn] + "htt_"+chn+".inputs-sm-13TeV"+postfix+".root",
                                                                  "$BIN/$PROCESS$MASS",
                                                                  "$BIN/$PROCESS$MASS_$SYSTEMATIC");
        }
    }
    
    
    
    //cb.FilterSysts([&](ch::Systematic *s){
    //  std::cout << s->shape_u()->Integral() << std::endl;
    //  return 1;
      //return !(s->shape_u()->Integral()> 0. && s->shape_d()->Integral()>0.);
    //});
    
    //Now delete processes with 0 yield
    cb.FilterProcs([&](ch::Process *p) {
        bool null_yield = !(p->rate() > 0. /*|| BinIsControlRegion(p)*/);
        if (null_yield){
            std::cout << "[Null yield] Removing process with null yield: \n ";
            std::cout << ch::Process::PrintHeader << *p << "\n";
            cb.FilterSysts([&](ch::Systematic *s){
                bool remove_syst = (MatchingProcess(*p,*s));
                return remove_syst;
            });
        }
        return null_yield;
    });
        
    
    
    
    // And convert any shapes in the CRs to lnN:
    // Convert all shapes to lnN at this stage
    cb.cp().FilterSysts(BinIsNotControlRegion).syst_type({"shape"}).ForEachSyst([](ch::Systematic *sys) {
        sys->set_type("lnN");
        if(sys->value_d() <0.001) {sys->set_value_d(0.001);};
        if(sys->value_u() <0.001) {sys->set_value_u(0.001);};
    });
    
    std::vector<std::string> all_prefit_bkgs = {
        "QCD","ZL","ZJ","ZTT","TTJ","TTT","TT",
        "W","W_rest","ZJ_rest","TTJ_rest","VVJ_rest","VV","VVT","VVJ",
        "ggH_hww125","qqH_hww125","EWKZ", "qqHsm_htt125", "qqH_htt125", "WH_htt125", "ZH_htt125"};
     
         
    if(!real_data){
         for (auto b : cb.cp().FilterAll(BinIsControlRegion).bin_set()) {
             std::cout << " - Replacing data with asimov in bin " << b << "\n";
             cb.cp().bin({b}).ForEachObs([&](ch::Observation *obs) {
               obs->set_shape(cb.cp().bin({b}).backgrounds().process(all_prefit_bkgs).GetShape()+cb.cp().bin({b}).signals().process({"ggHsm_htt", "ggH_htt"}).mass({"125"}).GetShape(), true);
               obs->set_rate(cb.cp().bin({b}).backgrounds().process(all_prefit_bkgs).GetRate()+cb.cp().bin({b}).signals().process({"ggHsm_htt", "ggH_htt"}).mass({"125"}).GetRate());
             });
           }
   }    
    
    //     //Merge to one bin for control region bins
    //    cb.cp().FilterAll(BinIsNotControlRegion).ForEachProc(To1Bin<ch::Process>);
    //    cb.cp().FilterAll(BinIsNotControlRegion).ForEachObs(To1Bin<ch::Observation>);
    
    // can auto-merge the bins with bbb uncertainty > 90% - may be better to merge these bins by hand though!
    auto rebin = ch::AutoRebin()
    .SetBinThreshold(0.)
    .SetBinUncertFraction(0.9)
    .SetRebinMode(1)
    .SetPerformRebin(true)
    .SetVerbosity(1);
    if(auto_rebin) rebin.Rebin(cb, cb);
  
  
    // At this point we can fix the negative bins
    std::cout << "Fixing negative bins\n";
    cb.ForEachProc([](ch::Process *p) {
      if (ch::HasNegativeBins(p->shape())) {
         std::cout << "[Negative bins] Fixing negative bins for " << p->bin()
                   << "," << p->process() << "\n";
         //std::cout << "[Negative bins] Before:\n";
         //p->shape()->Print("range");
        auto newhist = p->ClonedShape();
        ch::ZeroNegativeBins(newhist.get());
        // Set the new shape but do not change the rate, we want the rate to still
        // reflect the total integral of the events
        p->set_shape(std::move(newhist), false);
         //std::cout << "[Negative bins] After:\n";
         //p->shape()->Print("range");
      }
    });
  
    cb.ForEachSyst([](ch::Systematic *s) {
      if (s->type().find("shape") == std::string::npos) return;            
      if (ch::HasNegativeBins(s->shape_u()) || ch::HasNegativeBins(s->shape_d())) {
         std::cout << "[Negative bins] Fixing negative bins for syst" << s->bin()
               << "," << s->process() << "," << s->name() << "\n";
         //std::cout << "[Negative bins] Before:\n";
         //s->shape_u()->Print("range");
         //s->shape_d()->Print("range");
        auto newhist_u = s->ClonedShapeU();
        auto newhist_d = s->ClonedShapeD();
        ch::ZeroNegativeBins(newhist_u.get());
        ch::ZeroNegativeBins(newhist_d.get());
        // Set the new shape but do not change the rate, we want the rate to still
        // reflect the total integral of the events
        s->set_shapes(std::move(newhist_u), std::move(newhist_d), nullptr);
         //std::cout << "[Negative bins] After:\n";
         //s->shape_u()->Print("range");
         //s->shape_d()->Print("range");
      }
  });
      
    
    ////! [part8]
    auto bbb = ch::BinByBinFactory()
    .SetPattern("CMS_$ANALYSIS_$BIN_$ERA_$PROCESS_bin_$#")
    .SetAddThreshold(0.05)
    .SetMergeThreshold(0.8)
    .SetFixNorm(false);
    bbb.MergeBinErrors(cb.cp().backgrounds().FilterProcs(BinIsControlRegion));
    bbb.AddBinByBin(cb.cp().backgrounds().FilterProcs(BinIsControlRegion), cb);
    // To be on the safe side we don't want to merge any bin uncertainties for Higgs events
    bbb.MergeBinErrors(cb.cp().process({"qqH_htt","qqHsm_htt","qqHmm_htt","qqHps_htt","WH_htt","WHsm_htt","WHps_htt","WHmm_htt","ZH_htt","ZHsm_htt","ZHmm_htt","ZHps_htt","qqH_htt125","qqHsm_htt125","qqHmm_htt125","qqHps_htt125","WH_htt125","WHsm_htt125","WHps_htt125","WHmm_htt125","ZH_htt125","ZHsm_htt125","ZHmm_htt125","ZHps_htt125"}, false).FilterProcs(BinIsNotControlRegion));

    auto bbb_sig = ch::BinByBinFactory()
    .SetPattern("CMS_$ANALYSIS_$BIN_$ERA_$PROCESS_bin_$#")
    .SetAddThreshold(0.05)
    .SetMergeThreshold(0.0)
    .SetFixNorm(false);
    bbb_sig.AddBinByBin(cb.cp().signals(), cb); 
    
    // And now do bbb for the control region with a slightly different config:
    auto bbb_ctl = ch::BinByBinFactory()
    .SetPattern("CMS_$ANALYSIS_$BIN_$ERA_$PROCESS_bin_$#")
    .SetAddThreshold(0.)
    .SetMergeThreshold(0.8)
    .SetFixNorm(false)
    .SetVerbosity(1);
    // Will merge but only for non W and QCD processes, to be on the safe side
    bbb_ctl.MergeBinErrors(cb.cp().process({"QCD", "W"}, false).FilterProcs(BinIsNotControlRegion));
    bbb_ctl.AddBinByBin(cb.cp().process({"QCD", "W"}, false).FilterProcs(BinIsNotControlRegion), cb);
    
    
    
    // This function modifies every entry to have a standardised bin name of
    // the form: {analysis}_{channel}_{bin_id}_{era}
    // which is commonly used in the htt analyses
    ch::SetStandardBinNames(cb);
    //! [part8]
    

    //! [part9]
    // First we generate a set of bin names:
    
    
    //Write out datacards. Naming convention important for rest of workflow. We
    //make one directory per chn-cat, one per chn and cmb. In this code we only
    //store the individual datacards for each directory to be combined later, but
    //note that it's also possible to write out the full combined card with CH
    string output_prefix = "output/";
    if(output_folder.compare(0,1,"/") == 0) output_prefix="";
    ch::CardWriter writer(output_prefix + output_folder + "/$TAG/$MASS/$BIN.txt",
                          output_prefix + output_folder + "/$TAG/common/htt_input.root");
    
    
    writer.WriteCards("cmb", cb);
    for (auto chn : cb.channel_set()) {

        // per-channel
        writer.WriteCards(chn, cb.cp().channel({chn}));
        // And per-channel-category
        writer.WriteCards("htt_"+chn+"_1_13TeV", cb.cp().channel({chn}).bin_id({1}));
        writer.WriteCards("htt_"+chn+"_2_13TeV", cb.cp().channel({chn}).bin_id({2}));
        writer.WriteCards("htt_"+chn+"_3_13TeV", cb.cp().channel({chn}).bin_id({3}));
        writer.WriteCards("htt_"+chn+"_4_13TeV", cb.cp().channel({chn}).bin_id({4}));
        
        
        for (auto mmm : {"125"}){
            
            
            if (control_region > 0){
                
                if (ch::contains({"et", "mt"}, chn)) {
                    
                
                    cb.cp().channel({chn}).bin_id({10}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/"+chn+"/"+mmm+ "/htt_"+chn+"_10_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"10.root");
                    cb.cp().channel({chn}).bin_id({11}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/"+chn+"/"+mmm+ "/htt_"+chn+"_11_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"11.root");
                    cb.cp().channel({chn}).bin_id({12}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/"+chn+"/"+mmm+ "/htt_"+chn+"_12_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"12.root");
                    cb.cp().channel({chn}).bin_id({13}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/"+chn+"/"+mmm+ "/htt_"+chn+"_13_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"13.root");
                    cb.cp().channel({chn}).bin_id({14}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/"+chn+"/"+mmm+ "/htt_"+chn+"_14_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"14.root");
                    cb.cp().channel({chn}).bin_id({15}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/"+chn+"/"+mmm+ "/htt_"+chn+"_15_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"15.root");
                    cb.cp().channel({chn}).bin_id({16}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/"+chn+"/"+mmm+ "/htt_"+chn+"_16_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"16.root");
                    cb.cp().channel({chn}).bin_id({17}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/"+chn+"/"+mmm+ "/htt_"+chn+"_17_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"17.root");
                    cb.cp().channel({chn}).bin_id({18}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/"+chn+"/"+mmm+ "/htt_"+chn+"_18_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"18.root");
                    cb.cp().channel({chn}).bin_id({20}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/"+chn+"/"+mmm+ "/htt_"+chn+"_20_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"20.root");
                    
                        
                    cb.cp().channel({chn}).bin_id({10}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/cmb/"+mmm+ "/htt_"+chn+"_10_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"10.root");
                    cb.cp().channel({chn}).bin_id({11}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/cmb/"+mmm+ "/htt_"+chn+"_11_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"11.root");
                    cb.cp().channel({chn}).bin_id({12}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/cmb/"+mmm+ "/htt_"+chn+"_12_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"12.root");
                    cb.cp().channel({chn}).bin_id({13}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/cmb/"+mmm+ "/htt_"+chn+"_13_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"13.root");
                    cb.cp().channel({chn}).bin_id({14}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/cmb/"+mmm+ "/htt_"+chn+"_14_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"14.root");
                    cb.cp().channel({chn}).bin_id({15}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/cmb/"+mmm+ "/htt_"+chn+"_15_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"15.root");
                    cb.cp().channel({chn}).bin_id({16}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/cmb/"+mmm+ "/htt_"+chn+"_16_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"16.root");
                    cb.cp().channel({chn}).bin_id({17}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/cmb/"+mmm+ "/htt_"+chn+"_17_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"17.root");
                    cb.cp().channel({chn}).bin_id({18}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/cmb/"+mmm+ "/htt_"+chn+"_18_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"18.root");
                    cb.cp().channel({chn}).bin_id({20}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/cmb/"+mmm+ "/htt_"+chn+"_20_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"20.root");
                    
                } // end et mt
                if (ch::contains({"tt"}, chn)) {
                    
                    cb.cp().channel({chn}).bin_id({10}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/"+chn+"/"+mmm+ "/htt_"+chn+"_10_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"10.root");
                    cb.cp().channel({chn}).bin_id({11}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/"+chn+"/"+mmm+ "/htt_"+chn+"_11_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"11.root");
                    cb.cp().channel({chn}).bin_id({12}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/"+chn+"/"+mmm+ "/htt_"+chn+"_12_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"12.root");
                    cb.cp().channel({chn}).bin_id({13}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/"+chn+"/"+mmm+ "/htt_"+chn+"_13_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"13.root");
                        
                        
                    cb.cp().channel({chn}).bin_id({10}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/cmb/"+mmm+ "/htt_"+chn+"_10_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"10.root");
                    cb.cp().channel({chn}).bin_id({11}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/cmb/"+mmm+ "/htt_"+chn+"_11_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"11.root");
                    cb.cp().channel({chn}).bin_id({12}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/cmb/"+mmm+ "/htt_"+chn+"_12_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"12.root");
                    cb.cp().channel({chn}).bin_id({13}).mass({"$MASS", "*"}).WriteDatacard(output_prefix + output_folder +"/cmb/"+mmm+ "/htt_"+chn+"_13_13TeV.txt", output_prefix + output_folder +"/"+chn+ "/common/htt_input"+chn+"13.root");
                } // end tt
            } // end CR
        }
    }
    
    
    cb.PrintAll();
    cout << " done\n";
    
    
}
