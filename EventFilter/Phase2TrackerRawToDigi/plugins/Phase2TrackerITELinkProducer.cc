#include <memory>

#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/one/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"

#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/TrackReco/interface/TrackFwd.h"
#include "DataFormats/Common/interface/DetSetVectorNew.h"
#include "DataFormats/Common/interface/Handle.h"
#include "DataFormats/Phase2TrackerDigi/interface/Phase2TrackerDigi.h"
#include "DataFormats/FEDRawData/interface/FEDRawDataCollection.h"
#include "DataFormats/FEDRawData/interface/FEDRawData.h"
#include "DataFormats/FEDRawData/interface/FEDHeader.h"
#include "DataFormats/FEDRawData/interface/FEDTrailer.h"
#include "CondFormats/SiPhase2TrackerObjects/interface/TrackerDetToDTCELinkCablingMap.h"
#include "CondFormats/SiPhase2TrackerObjects/interface/DTCELinkId.h"
#include "CondFormats/DataRecord/interface/TrackerDetToDTCELinkCablingMapRcd.h"
#include "Geometry/TrackerGeometryBuilder/interface/TrackerGeometry.h"
#include "Geometry/Records/interface/TrackerDigiGeometryRecord.h"
#include "DataFormats/TrackerCommon/interface/TrackerTopology.h"
#include "Geometry/CommonTopologies/interface/PixelGeomDetUnit.h"
#include "Geometry/CommonTopologies/interface/PixelTopology.h"

class Phase2TrackerITELinkProducer : public edm::one::EDProducer<> {
public:
    explicit Phase2TrackerITELinkProducer(const edm::ParameterSet&);
    ~Phase2TrackerITELinkProducer() override;

private:
    void produce(edm::Event&, const edm::EventSetup&) override;
    
    const edm::ESGetToken<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd> cablingMapToken_;
    const edm::ESGetToken<TrackerGeometry, TrackerDigiGeometryRecord> trackerGeometryToken_;
    const edm::ESGetToken<TrackerTopology, TrackerTopologyRcd> trackerTopologyToken_;

};

Phase2TrackerITELinkProducer::Phase2TrackerITELinkProducer(const edm::ParameterSet& iConfig)
    : cablingMapToken_(esConsumes()),
      trackerGeometryToken_(esConsumes<TrackerGeometry, TrackerDigiGeometryRecord>()),
      trackerTopologyToken_(esConsumes<TrackerTopology, TrackerTopologyRcd>())
{
    produces<FEDRawDataCollection>();
}

Phase2TrackerITELinkProducer::~Phase2TrackerITELinkProducer() {
}

void Phase2TrackerITELinkProducer::produce(edm::Event& iEvent, const edm::EventSetup& iSetup) 
{
    // Retrieve TrackerGeometry and TrackerTopology from EventSetup
    const TrackerGeometry& trackerGeometry = iSetup.getData(trackerGeometryToken_);
    const TrackerTopology& trackerTopology = iSetup.getData(trackerTopologyToken_);
    
    // Retrieve the CablingMap
    const auto& cablingMap = iSetup.getData(cablingMapToken_);

}

DEFINE_FWK_MODULE(Phase2TrackerITELinkProducer);
