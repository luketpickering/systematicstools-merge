////////////////////////////////////////////////////////////////////////
// Class:       LArSystResponseTreeMaker
// Plugin Type: analyzer (art v2_10_03)
// File:        LArSystResponseTreeMaker_module.cc
//
// Generated at Mon Apr 16 13:24:50 2018 by Luke Pickering using cetskelgen
// from cetlib version v3_02_00.
////////////////////////////////////////////////////////////////////////

#include "larsyst/interface/EventResponse_product.hh"
#include "larsyst/interface/types.hh"

#include "larsyst/interpreters/EventSplineCacheHelper.hh"
#include "larsyst/interpreters/ParamHeaderHelper.hh"
#include "larsyst/interpreters/load_parameter_headers.hh"

#include "larsyst/utility/md5.hh"

#include "art/Framework/Core/EDAnalyzer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Principal/Run.h"
#include "art/Framework/Principal/SubRun.h"
#include "art/Framework/Services/Optional/TFileService.h"
#include "canvas/Utilities/InputTag.h"
#include "fhiclcpp/ParameterSet.h"
#include "messagefacility/MessageLogger/MessageLogger.h"

#include "TTree.h"

using namespace larsyst;
class LArSystResponseTreeMaker;

class LArSystEventResponseTree {
  TTree *tree;

  // Data members
  ULong_t event;
  ULong_t t_it;
  std::map<paramId_t, double> param_values;
  std::map<paramId_t, double> event_responses;
  double total_weight;

public:
  LArSystEventResponseTree() : tree{nullptr} {};

  void SetTree(TTree *t) { tree = t; }
  void Fill() { tree->Fill(); }

  void SetEvent(ULong_t ev) { event = ev; }
  void SetThrow(ULong_t t) { t_it = t; }
  void SetParamResponse(paramId_t i, double v, double r) {
    param_values.at(i) = v;
    event_responses.at(i) = r;
  }
  void SetTotalWeight(double w) { total_weight = w; }

  void MakeBranches(param_header_map_t const &param_map = {},
                    bool isThrows = false) {
    if (!tree) {
      std::cout << "[ERROR]: Attempted to make tree branches before setting "
                   "the response tree."
                << std::endl;
      throw;
    }
    tree->Branch("event", &event, "event/l");
    if (isThrows) {
      tree->Branch("throw", &t_it, "throw/l");
    }
    tree->Branch("total_weight", &total_weight, "total_weight/D");

    // Have to initialize full map first to ensure that reallocation doesn't
    // happen after the branches have been built
    for (auto &sph : param_map) {
      param_values[sph.first] = sph.second.second.centralParamValue;
      event_responses[sph.first] =
          sph.second.second.isWeightSystematicVariation ? 1 : 0;
    }

    for (auto &sph : param_map) {
      std::string pname = std::string("param_") + std::to_string(sph.first) +
                          "_" + sph.second.second.prettyName;
      tree->Branch((pname + "_value").c_str(), &param_values[sph.first],
                   (pname + "_value/D").c_str());
      tree->Branch((pname + "_response").c_str(), &event_responses[sph.first],
                   (pname + "_response/D").c_str());
    }
  }
};

class LArSystResponseTreeMaker : public art::EDAnalyzer {
public:
  explicit LArSystResponseTreeMaker(fhicl::ParameterSet const &p);
  // The compiler-generated destructor is fine for non-base
  // classes without bare pointers or other resource use.

  // Plugins should not be copied or assigned.
  LArSystResponseTreeMaker(LArSystResponseTreeMaker const &) = delete;
  LArSystResponseTreeMaker(LArSystResponseTreeMaker &&) = delete;
  LArSystResponseTreeMaker &
  operator=(LArSystResponseTreeMaker const &) = delete;
  LArSystResponseTreeMaker &operator=(LArSystResponseTreeMaker &&) = delete;

  // Required functions.
  void analyze(art::Event const &e) override;

private:
  art::InputTag fInpTag;
  LArSystEventResponseTree fOutputTree;
  double fTweak;
  bool fSplineMode;

  param_header_map_t configuredParameterHeaders;

  EventSplineCache<ULong_t, ParamValidationAndErrorResponse::kTortoise>
      fEventHelper;

  ParamHeaderHelper fHeaderHelper;
};

LArSystResponseTreeMaker::LArSystResponseTreeMaker(fhicl::ParameterSet const &p)
    : EDAnalyzer(p) {

  if (!p.has_key("generated_systematic_provider_configuration")) {
    std::cout << "[ERROR]: Could not find producer key: "
                 "\"generated_systematic_provider_configuration\". This should "
                 "contain a "
                 "list of configured systematic providers generated by "
                 "GenerateSystProviderConfig."
              << std::endl;
    throw;
  }

  fhicl::ParameterSet syst_provider_config =
      p.get<fhicl::ParameterSet>("generated_systematic_provider_configuration");
  std::string sp_config_hash = md5(syst_provider_config.to_compact_string());

  std::cout << "[INFO]: md5 of systematic provider configuration: "
            << sp_config_hash << std::endl;

  fInpTag =
      art::InputTag(p.get<std::string>("input_module_label"), sp_config_hash);

  configuredParameterHeaders = load_syst_provider_headers(syst_provider_config);
  if (!configuredParameterHeaders.size()) {
    std::cout << "[ERROR]: Expected to find some headers." << std::endl;
    throw;
  }

  if (configuredParameterHeaders.begin()->second.second.isSplineable) {
    fEventHelper.SetHeaders(configuredParameterHeaders);
    std::cout << "[INFO]: Running TreeMaker in spline mode." << std::endl;
    fSplineMode = true;
    fTweak = p.get<double>("param_tweak");
  } else {
    std::cout << "[INFO]: Running TreeMaker in multisim mode." << std::endl;
    fHeaderHelper.SetHeaders(configuredParameterHeaders);
    fSplineMode = false;
  }

  ParamValidationAndErrorResponse errSettings;
  errSettings.fCare = ParamValidationAndErrorResponse::kTortoise;
  errSettings.fPedantry = ParamValidationAndErrorResponse::kNotOnMyWatch;
  if (fSplineMode) {
    fEventHelper.SetChkErr(errSettings);
    for (auto const &sph_it : configuredParameterHeaders) {
      fEventHelper.DeclareUsingParameter(sph_it.first, fTweak);
    }
  } else {
    fHeaderHelper.SetChkErr(errSettings);
  }
  art::ServiceHandle<art::TFileService> tfs;
  fOutputTree.SetTree(tfs->make<TTree>("LArSystEventResponseTree", ""));
  fOutputTree.MakeBranches(configuredParameterHeaders, !fSplineMode);
}

void LArSystResponseTreeMaker::analyze(art::Event const &e) {
  art::Handle<EventResponse> er;
  e.getByLabel(fInpTag, er);

  if (!er.isValid()) {
    std::cout << "[ERROR]: Could not find matching input data product: "
              << fInpTag << std::endl;
    e.getByLabel(fInpTag, er);
    // Not manually throwing here, as this is the edge case where ART wants you
    // to access the null pointer and let the framework deal with the fallout.
  }

  if (fSplineMode) {
    ULong_t event_resp_ctr = 0;
    size_t ev_it = fEventHelper.GetNEventsInCache();
    std::cout << "[INFO]: Reading event " << ev_it
              << " responses: " << std::endl;
    for (auto &eur : er->responses) {
      for (auto irmap : eur) {
        std::cout << "\t ParamId: " << irmap.first << " with "
                  << irmap.second.size() << std::endl;
      }
      fEventHelper.CacheEvent(event_resp_ctr++, eur);
    }
    size_t ev_it_end = fEventHelper.GetNEventsInCache();

    for (; ev_it < ev_it_end; ++ev_it) {
      std::cout << "[INFO]: Calculating weights for cache event " << ev_it
                << std::endl;
      fOutputTree.SetEvent(ev_it);
      for (auto &sph : configuredParameterHeaders) {
        fOutputTree.SetParamResponse(
            sph.first, fTweak,
            sph.second.second.isWeightSystematicVariation
                ? fEventHelper.GetEventWeightResponse(sph.first, ev_it)
                : fEventHelper.GetEventLateralResponse(sph.first, ev_it));
      }
      fOutputTree.SetTotalWeight(
          fEventHelper.GetTotalEventWeightResponse(ev_it));
      fOutputTree.Fill();
    }
  } else {
    static ULong_t ev_ctr = 0;
    static auto const &params = fHeaderHelper.GetParameters();
    static auto const &npt = fHeaderHelper.GetNDiscreteVariations(params);
    static ULong_t nvars = (*std::min_element(npt.begin(), npt.end()));
    static auto const &var_vals =
        fHeaderHelper.GetDiscreteVariationParameterValues(params);
    for (auto &eur : er->responses) {
      fOutputTree.SetEvent(ev_ctr++);
      for (size_t t = 0; t < nvars; ++t) {
        fOutputTree.SetThrow(t);
        double tweight = 1;
        for (auto &sph : params) {
          if (eur.find(sph) != eur.end()) {
            double w = fHeaderHelper.GetDiscreteResponse(sph, t, eur);
            tweight *= w;
            fOutputTree.SetParamResponse(sph, var_vals.at(sph)[t], w);
          } else {
            fOutputTree.SetParamResponse(sph, var_vals.at(sph)[t], 0xdeadb33f);
          }
        }
        fOutputTree.SetTotalWeight(tweight);
        fOutputTree.Fill();
      }
    }
  }
}

DEFINE_ART_MODULE(LArSystResponseTreeMaker)
