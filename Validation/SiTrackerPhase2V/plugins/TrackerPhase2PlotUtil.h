// -*- C++ -*-
// Package:    Validation/SiTrackerPhase2V
// Class:      TrackerPhase2PlotUtil
// Description: Renders the MonitorElements of one DQM folder to image files
//
// Author: Lacey Dishman, Sihyun Jeon (Boston University)
// Written: August 2026

#ifndef Validation_SiTrackerPhase2V_plugins_TrackerPhase2PlotUtil_H
#define Validation_SiTrackerPhase2V_plugins_TrackerPhase2PlotUtil_H

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "DQMServices/Core/interface/DQMStore.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"

#include "TAxis.h"

class TF1;
class TFile;
class TH1;

namespace TrackerPhase2PlotUtil {

  // Under/overflow goes into the first/last of nBins uniform bins over [lo, hi), so it stays visible in the plots
  inline double clampToRange(double value, double lo, double hi, int nBins) {
    const double halfBin = 0.5 * (hi - lo) / nBins;
    if (value < lo)
      return lo + halfBin;
    if (value >= hi)
      return hi - halfBin;
    return value;
  }

  inline double clampToAxis(double value, const TAxis& axis) {
    if (value < axis.GetXmin())
      return axis.GetBinCenter(1);
    if (value >= axis.GetXmax())
      return axis.GetBinCenter(axis.GetNbins());
    return value;
  }

  // Books a 1D histogram with the binning of the PSet (NxBins, xmin, xmax) under its own name and title, for the
  // families of histograms that share one configuration, e.g. one per section or per DTC
  template <typename IBooker>
  auto book1DLike(const edm::ParameterSet& hpars, IBooker& ibooker, const std::string& name, const std::string& title) {
    return ibooker.book1D(name,
                          title,
                          hpars.getParameter<int32_t>("NxBins"),
                          hpars.getParameter<double>("xmin"),
                          hpars.getParameter<double>("xmax"));
  }

  // The title of the PSet with ", <suffix>" added to its histogram title, in front of the axis titles
  inline std::string titleWithSuffix(const edm::ParameterSet& hpars, const std::string& suffix) {
    const std::string title = hpars.getParameter<std::string>("title");
    const size_t axes = title.find(';');
    return title.substr(0, axes) + ", " + suffix + (axes == std::string::npos ? "" : title.substr(axes));
  }

  // Gaussian fit within mean +- 4 RMS; null if the histogram is empty or the fit fails
  std::unique_ptr<TF1> fitGaussian(TH1& h);

  struct PlotConfig {
    std::string plotDir = ".";
    std::vector<std::string> formats = {"png", "pdf"};
    // MEs with this prefix get the occupancy-map rendering
    std::string occMapNamePrefix;
    double zMax = 1.6;
    // y-axis maximum of the stacked plots, automatic if not positive
    double stackedYMax = 0.;
    // 1D MEs with this prefix get a Gaussian fit drawn on top
    std::string gaussFitNamePrefix;
    // The fit box of '<gaussFitNamePrefix>_<id>' starts with the line '<gaussFitLabelPrefix><id>', e.g. 'DTC 11'
    std::string gaussFitLabelPrefix;
    // y-axis maximum of the Gaussian-fit plots, automatic if not positive
    double gaussFitYMax = 0.;
  };

  int saveFolderPlots(dqm::harvesting::DQMStore::IGetter& igetter, const std::string& folder, const PlotConfig& cfg);

  // Renders components as '<baseName>Stacked' and '<baseName>Overlaid'
  int saveStackedPlots(const std::vector<std::pair<std::string, const TH1*>>& components,
                       const std::string& baseName,
                       const PlotConfig& cfg);

  // Renders the 2D maps as one figure '<name>', one pad per panel stacked vertically
  int savePanelPlots(const std::vector<std::pair<std::string, const TH1*>>& panels,
                     const std::string& name,
                     const PlotConfig& cfg);

  std::unique_ptr<TH1> readHistFromDQMFile(TFile& file, const std::string& folder, const std::string& name);

}  // namespace TrackerPhase2PlotUtil

#endif  // Validation_SiTrackerPhase2V_plugins_TrackerPhase2PlotUtil_H
