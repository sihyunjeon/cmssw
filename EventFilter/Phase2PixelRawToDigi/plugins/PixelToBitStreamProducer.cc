// -*- C++ -*-
// Package:    EventFilter/Phase2PixelRawToDigi
// Class:      PixelToBitStreamProducer
// Description: Make Phase2ITQCore objects for digis
// Maintainer: Si Hyun Jeon, shjeon@cern.ch
// Original Author:  Rohan Misra
// Created:  Thu, 30 Sep 2021 02:04:06 GMT
//

// system include files
#include <memory>

// user include files
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/stream/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "FWCore/Utilities/interface/transform.h"
#include "FWCore/Utilities/interface/StreamID.h"
#include "DataFormats/Math/interface/Point3D.h"
#include "DataFormats/Common/interface/DetSetVector.h"
#include "DataFormats/DetId/interface/DetId.h"
#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITDigiHit.h"
#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITQCore.h"
#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITChip.h"
#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITChipBitStream.h"
#include "DataFormats/SiPixelCluster/interface/SiPixelCluster.h"
#include "DataFormats/SiPixelDetId/interface/PixelSubdetector.h"
#include "DataFormats/SiPixelDigi/interface/PixelDigi.h"
#include "DataFormats/TrackerCommon/interface/TrackerTopology.h"
#include "TrackingTools/Records/interface/TransientTrackRecord.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/Phase2DAQFormatSpecification.h"

using namespace Phase2DAQFormatSpecification;

class PixelToBitStreamProducer : public edm::stream::EDProducer<> {
public:
  PixelToBitStreamProducer(const edm::ParameterSet&);
  ~PixelToBitStreamProducer() override = default;

private:
  void produce(edm::Event&, const edm::EventSetup&) override;

  const edm::InputTag src_;
  const edm::EDGetTokenT<edm::DetSetVector<PixelDigi>> pixelDigiToken_;
  const edm::ESGetToken<TrackerTopology, TrackerTopologyRcd> tTopoToken_;
  // Gap-pixel handling policy
  //  - false (default): drop hits in the inter-chip gap region
  //  - true: keep gap-region hits with their original coordinates (needed for full packing/unpacking without information loss)
  const bool keepGapPixels_;
  typedef math::XYZPointD Point;
  typedef std::vector<Point> PointCollection;
};

PixelToBitStreamProducer::PixelToBitStreamProducer(const edm::ParameterSet& iConfig)
    : src_(iConfig.getParameter<edm::InputTag>("src")),
      pixelDigiToken_(consumes(iConfig.getParameter<edm::InputTag>("siPixelDigi"))),
      tTopoToken_(esConsumes()),
      keepGapPixels_(iConfig.getUntrackedParameter<bool>("keepGapPixels", false)) {
  produces<edm::DetSetVector<Phase2ITQCore>>();
  produces<edm::DetSetVector<Phase2ITChipBitStream>>();
}

namespace {
  // Dimension for 1 chip module (PXB Layer1, 3dim) = 672 X 216
  // Dimension for 2 chips module = 672 X 434 = 672 X (216 + 1 + 216 + 1)
  // Dimension for 4 chips module = 1354 X 434 = (672 + 5 + 672 + 5) X (216 + 1 + 216 + 1)
  // Spacing 1 in column and 5 in row is introduced for each chip in between
  // if neighboring chip exists
  constexpr int kQCoresInChipRow = (672);
  constexpr int kQCoresInChipColumn = (216);
  constexpr int kQCoresInChipRowGap = (5);
  constexpr int kQCoresInChipColumnGap = (1);
}

bool isInGapRegion(const Phase2ITDigiHit& hit) {
  int r = hit.row();
  int c = hit.col();
  bool rowInGap = (r >= kQCoresInChipRow) && (r < kQCoresInChipRow + 2 * kQCoresInChipRowGap);
  bool colInGap = (c >= kQCoresInChipColumn) && (c < kQCoresInChipColumn + 2 * kQCoresInChipColumnGap);
  return rowInGap || colInGap;
}

std::vector<Phase2ITChip> splitByChip(const std::vector<Phase2ITDigiHit>& hitList, uint32_t detId = 0) {
  std::array<std::vector<Phase2ITDigiHit>, CHIPS_PER_MODULE> hitsPerChip;
  for (auto hit : hitList) {
    int chipIndex = (hit.col() < kQCoresInChipColumn) ? 0 : 1;
    if (hit.row() >= kQCoresInChipRow) {
      chipIndex += 2;
    }
    int rowOffset = (chipIndex >= 2) ? kQCoresInChipRow : 0;
    int colOffset = (chipIndex % 2 == 1) ? kQCoresInChipColumn : 0;
    hit.set_row(hit.row() - rowOffset);
    hit.set_col(hit.col() - colOffset);
    hitsPerChip[chipIndex].push_back(hit);
  }

  // Generate Phase2ITChip objects from the hit lists
  std::vector<Phase2ITChip> chips;
  chips.reserve(CHIPS_PER_MODULE);
  for (int chipIndex = 0; chipIndex < CHIPS_PER_MODULE; chipIndex++) {
    chips.push_back(Phase2ITChip(chipIndex, hitsPerChip[chipIndex], detId));
  }

  return chips;
}

std::vector<Phase2ITChip> processHits(std::vector<Phase2ITDigiHit> hitList, bool keepGapPixels, uint32_t detId = 0) {
  if (!keepGapPixels) {
    // Drop gap-region hits entirely.
    hitList.erase(std::remove_if(hitList.begin(), hitList.end(), &isInGapRegion), hitList.end());
  } // When keepGapPixels is true, all hits are processed with with original coordinates
  return splitByChip(hitList, detId);
}

// ------------ method called to produce the data  ------------
void PixelToBitStreamProducer::produce(edm::Event& iEvent, const edm::EventSetup& iSetup) {
  using namespace edm;
  using namespace std;

  unique_ptr<edm::DetSetVector<Phase2ITQCore>> aQCoreVector = make_unique<edm::DetSetVector<Phase2ITQCore>>();
  unique_ptr<edm::DetSetVector<Phase2ITChipBitStream>> aBitStreamVector =
      make_unique<edm::DetSetVector<Phase2ITChipBitStream>>();

  auto const& tTopo = iSetup.getData(tTopoToken_);

  auto pixelDigiHandle = iEvent.getHandle(pixelDigiToken_);
  const edm::DetSetVector<PixelDigi>& pixelDigi = *pixelDigiHandle;
  for (const auto& theDigis : pixelDigi) {
    DetId tkId = theDigis.id;
    std::vector<Phase2ITDigiHit> hitlist;
    std::vector<int> id;
    if (tkId.subdetId() == PixelSubdetector::PixelBarrel) {
      int layerNum = tTopo.pxbLayer(tkId.rawId());
      int ladderNum = tTopo.pxbLadder(tkId.rawId());
      int moduleNum = tTopo.pxbModule(tkId.rawId());
      id = {tkId.subdetId(), layerNum, ladderNum, moduleNum};
    } else if (tkId.subdetId() == PixelSubdetector::PixelEndcap) {
      int moduleNum = tTopo.pxfModule(tkId());
      int diskNum = tTopo.pxfDisk(tkId());
      int bladeNum = tTopo.pxfBlade(tkId());
      int panelNum = tTopo.pxfPanel(tkId());
      int sideNum = tTopo.pxfSide(tkId());
      id = {tkId.subdetId(), moduleNum, diskNum, bladeNum, panelNum, sideNum};
    }

    for (const auto& digi : theDigis) {
      hitlist.emplace_back(digi.row(), digi.column(), digi.adc());
    }
    std::vector<Phase2ITChip> chips = processHits(std::move(hitlist), keepGapPixels_, tkId.rawId());

    DetSet<Phase2ITQCore> DetSetQCores(tkId);
    DetSet<Phase2ITChipBitStream> DetSetBitStream(tkId);

    for (size_t i = 0; i < chips.size(); i++) {
      Phase2ITChip chip = chips[i];
      std::vector<Phase2ITQCore> qcores = chip.getOrganizedQCores();
      for (auto& qcore : qcores) {
        DetSetQCores.push_back(qcore);
      }
      Phase2ITChipBitStream aChipBitStream(i, chip.getChipCode());

      DetSetBitStream.push_back(aChipBitStream);
    }

    aBitStreamVector->insert(DetSetBitStream);
    aQCoreVector->insert(DetSetQCores);
  }
  iEvent.put(std::move(aQCoreVector));
  iEvent.put(std::move(aBitStreamVector));
}

DEFINE_FWK_MODULE(PixelToBitStreamProducer);
