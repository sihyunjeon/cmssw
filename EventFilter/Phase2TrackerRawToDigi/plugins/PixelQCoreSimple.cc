// -*- C++ -*-
// hi
// Package:    EventFilter/Phase2TrackerRawToDigi
// Class:      PixelQCoreSimple
//

/*
 Description: Make QCore objects for digis

 Implementation:
     [Notes on implementation]
*/
//
// Original Author:  Rohan Misra
//         Created:  Thu, 30 Sep 2021 02:04:06 GMT
//
//

// system include files
#include <memory>


// user include files
#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/stream/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"

#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/Utilities/interface/StreamID.h"

#include "DataFormats/Math/interface/Point3D.h"

#include "FWCore/Utilities/interface/InputTag.h"
#include "FWCore/Utilities/interface/transform.h"

#include "DataFormats/SiPixelDigi/interface/PixelDigi.h"
#include "DataFormats/Common/interface/DetSetVector.h"
#include "DataFormats/DetId/interface/DetId.h"

#include "DataFormats/Phase2TrackerDigi/interface/Hit.h"
#include "DataFormats/Phase2TrackerDigi/interface/QCore.h"
#include "DataFormats/Phase2TrackerDigi/interface/ReadoutChip.h"
#include "DataFormats/Phase2TrackerDigi/interface/ROCBitStream.h"

#include "DataFormats/SiPixelDetId/interface/PixelSubdetector.h"

#include "DataFormats/TrackerCommon/interface/TrackerTopology.h"

#include "DataFormats/SiPixelCluster/interface/SiPixelCluster.h"

#include "TrackingTools/Records/interface/TransientTrackRecord.h"


//
// class declaration
//

class PixelQCoreSimple : public edm::stream::EDProducer<> {
public:
  explicit PixelQCoreSimple(const edm::ParameterSet&);
  ~PixelQCoreSimple();

private:
  virtual void beginJob(const edm::EventSetup&) ;
  virtual void produce(edm::Event&, const edm::EventSetup&);
  virtual void endJob() ;

  // ----------member data ---------------------------
 
  edm::InputTag src_;
  edm::EDGetTokenT<edm::DetSetVector<PixelDigi>> pixelDigi_token_;
  const edm::DetSetVector<PixelDigi>* digis = NULL;
  edm::ESGetToken<TrackerTopology, TrackerTopologyRcd> const tTopoToken_;
};

//
// constants, enums and typedefs
//


//
// static data member definitions
//

//
// constructors and destructor
//
PixelQCoreSimple::PixelQCoreSimple(const edm::ParameterSet& iConfig)
    : tTopoToken_(esConsumes<TrackerTopology, TrackerTopologyRcd>())
{
  src_  = iConfig.getParameter<edm::InputTag>( "src" );
  //produces<PointCollection>( "innerPoint" ).setBranchAlias( "innerPoints");
  //produces<PointCollection>( "outerPoint" ).setBranchAlias( "outerPoints");
  //register your products
  
  pixelDigi_token_ = consumes<edm::DetSetVector<PixelDigi>>(iConfig.getParameter<edm::InputTag>("siPixelDigi"));

  //produces<int>("qcores").setBranchAlias( "qcores" );
  produces<edm::DetSetVector<QCore> >();
  produces<edm::DetSetVector<ROCBitStream> >();

/* Examples
  produces<ExampleData2>();

  //if do put with a label
  produces<ExampleData2>("label");
 
  //if you want to put into the Run
  produces<ExampleData2,InRun>();
*/
  //now do what ever other initialization is needed
}

PixelQCoreSimple::~PixelQCoreSimple() {
  // do anything here that needs to be done at destruction time
  // (e.g. close files, deallocate resources etc.)
  //
  // please remove this method altogether if it would be left empty
}

//
// member functions
//


// ------------ method called to produce the data  ------------
void PixelQCoreSimple::produce(edm::Event& iEvent, const edm::EventSetup& iSetup) {
  using namespace edm; 
  //using namespace reco; 
  using namespace std;
  
  unique_ptr<edm::DetSetVector<QCore> > aQCoreVector = make_unique<edm::DetSetVector<QCore> >();
  unique_ptr<edm::DetSetVector<ROCBitStream> > aBitStreamVector = make_unique<edm::DetSetVector<ROCBitStream> >();


  auto const &tTopo = iSetup.getData(tTopoToken_);

  edm::Handle<edm::DetSetVector<PixelDigi> > pixelDigiHandle;
  iEvent.getByToken(pixelDigi_token_, pixelDigiHandle);
  digis = pixelDigiHandle.product();
  int i =0;
    for (typename edm::DetSetVector<PixelDigi>::const_iterator DSVit = digis->begin(); DSVit != digis->end(); DSVit++) {
        i++;
    }
    std::cout<<i<<std::endl;
    i=0;
  edm::DetSetVector<PixelDigi>::const_iterator iterDet;
  for ( iterDet = pixelDigiHandle->begin();
        iterDet != pixelDigiHandle->end();
        iterDet++ ) {
    i++;
    DetId tkId = iterDet->id;

    edm::DetSet<PixelDigi> theDigis = (*pixelDigiHandle)[ tkId ];

    std::vector<Hit> hitlist;

    std::vector<int> id;
  }
  std::cout<<i<<std::endl;

}

// ------------ method called once each stream before processing any runs, lumis or events  ------------
//void PixelQCoreSimple::beginStream(edm::StreamID) {
  // please remove this method if not needed
//}

// ------------ method called once each stream after processing all runs, lumis and events  ------------
//void PixelQCoreSimple::endStream() {
  // please remove this method if not needed
//}

// ------------ method called when starting to processes a run  ------------
/*
void
PixelQCoreSimple::beginRun(edm::Run const&, edm::EventSetup const&)
{
}
*/

// ------------ method called when starting to processes a job  ------------
void
PixelQCoreSimple::beginJob(edm::EventSetup const&)
{
}


// ------------ method called when ending the processing of a run  ------------
/*
void
PixelQCoreSimple::endRun(edm::Run const&, edm::EventSetup const&)
{
}
*/

// ------------ method called when ending the processing of a job  ------------
void
PixelQCoreSimple::endJob()
{
}


// ------------ method called when starting to processes a luminosity block  ------------
/*
void
PixelQCoreSimple::beginLuminosityBlock(edm::LuminosityBlock const&, edm::EventSetup const&)
{
}
*/

// ------------ method called when ending the processing of a luminosity block  ------------
/*
void
PixelQCoreSimple::endLuminosityBlock(edm::LuminosityBlock const&, edm::EventSetup const&)
{
}
*/

// ------------ method fills 'descriptions' with the allowed parameters for the module  ------------
//void PixelQCoreSimple::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  //The following says we do not know what parameters are allowed so do no validation
  // Please change this to state exactly what you do use, even if it is no parameters
//  edm::ParameterSetDescription desc;
//  desc.setUnknown();
//  descriptions.addDefault(desc);
//}

//define this as a plug-in
DEFINE_FWK_MODULE(PixelQCoreSimple);

