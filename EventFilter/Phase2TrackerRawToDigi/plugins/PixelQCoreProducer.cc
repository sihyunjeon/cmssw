// -*- C++ -*-
// Package:    EventFilter/Phase2TrackerRawToDigi
// Class:      PixelQCoreProducer
// Description: Make QCore objects for digis
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
#include "DataFormats/Phase2TrackerDigi/interface/Hit.h"
#include "DataFormats/Phase2TrackerDigi/interface/QCore.h"
#include "DataFormats/Phase2TrackerDigi/interface/ReadoutChip.h"
#include "DataFormats/Phase2TrackerDigi/interface/ROCBitStream.h"
#include "DataFormats/SiPixelCluster/interface/SiPixelCluster.h"
#include "DataFormats/SiPixelDetId/interface/PixelSubdetector.h"
#include "DataFormats/SiPixelDigi/interface/PixelDigi.h"
#include "DataFormats/TrackerCommon/interface/TrackerTopology.h"
#include "TrackingTools/Records/interface/TransientTrackRecord.h"

class PixelQCoreProducer : public edm::stream::EDProducer<> {
    public:
        explicit PixelQCoreProducer(const edm::ParameterSet&);
        ~PixelQCoreProducer();

    private:
        virtual void beginJob(const edm::EventSetup&);
        virtual void produce(edm::Event&, const edm::EventSetup&);
        virtual void endJob() ;
 
    edm::InputTag src_;
    edm::EDGetTokenT<edm::DetSetVector<PixelDigi>> pixelDigi_token_;
    edm::ESGetToken<TrackerTopology, TrackerTopologyRcd> const tTopoToken_;
    typedef math::XYZPointD Point;
    typedef std::vector<Point> PointCollection;
};

PixelQCoreProducer::PixelQCoreProducer(const edm::ParameterSet& iConfig)
    : tTopoToken_(esConsumes<TrackerTopology, TrackerTopologyRcd>()){

    src_ = iConfig.getParameter<edm::InputTag>("src");
    pixelDigi_token_ = consumes<edm::DetSetVector<PixelDigi>>(iConfig.getParameter<edm::InputTag>("siPixelDigi"));

    produces<edm::DetSetVector<QCore> >();
    produces<edm::DetSetVector<ROCBitStream> >();
}

PixelQCoreProducer::~PixelQCoreProducer() {}

Hit updateHitCoordinatesForLargePixels(Hit &hit) {
    /*
        In-place modification of Hit coordinates to take into account large pixels
        Hits corresponding to large pixels are remapped so they lie on the boundary of the chip
        Note that this operation can produce multiple hits with the same row/column coordinates
        Duplicates get removed later on
    */

    // Current values before remapping
    int row = hit.row();
    int col = hit.col();

    // Values after remapping
    int updated_row = 0;
    int updated_col = 0;

    // Dimension for 2 chips module = 672 X 434 = 672 X (216 + 1 + 216 + 1)
    // Dimension for 4 chips module = 1354 X 434 = (672 + 5 + 672 + 5) X (216 + 1 + 216 + 1)
    // Spacing 1 in column and 5 in row is introduced for each chip in between
    // if neighboring chip exists

    // Remapping of the row coordinate
    if (row < 672) { updated_row = row; }
    else if(row <= 676) { updated_row = 671; } // This will be ignored for 2 chips module
    else if(row <= 681) { updated_row = 672; }
    else { updated_row = hit.row() - 10; }

    // Remapping of the column coordinate
    if(col < 216) { updated_col = col; }
    else if(col <= 216) { updated_col = 215; }
    else if(col <= 217) { updated_col = 216; }
    else { updated_col = hit.col() - 2; }

    hit.set_row(updated_row);
    hit.set_col(updated_col);

    return hit;
}

std::vector<Hit> adjustEdges(std::vector<Hit> hitList) {
    /*
        In-place modification of Hit coordinates to take into account large pixels
    */
    std::for_each(hitList.begin(), hitList.end(), &updateHitCoordinatesForLargePixels);
    return hitList;
}

std::vector<ReadoutChip> splitByChip(std::vector<Hit> hitList) {
    // Split the hit list by read out chip
    std::vector<std::vector<Hit>> hits_per_chip(4);
    for (auto hit:hitList) {
        int chip_index = (hit.col() < 216) ? 0 : 1;
        if (hit.row() >= 672){
            chip_index += 2;
        }
        hits_per_chip[chip_index].push_back(hit);
    }

    // Generate ReadoutChip objects from the hit lists
    std::vector<ReadoutChip> chips;
    for (int chip_index=0; chip_index<4; chip_index++) {
        chips.push_back(ReadoutChip(chip_index, hits_per_chip[chip_index]));
    }

    return chips;
}

std::vector<ReadoutChip> processHits(std::vector<Hit> hitList) {

    std::vector<Hit> newHitList;
    newHitList = adjustEdges(hitList);

    std::vector<ReadoutChip> chips = splitByChip(newHitList);
    std::vector<bool> code;

    for (size_t i = 0; i < chips.size(); i++) {
        ReadoutChip chip = chips[i];
        code = chip.get_chip_code();
    }

    return chips;
}

// ------------ method called to produce the data  ------------
void PixelQCoreProducer::produce(edm::Event& iEvent, const edm::EventSetup& iSetup) {
    using namespace edm; 
    using namespace std;
  
    unique_ptr<edm::DetSetVector<QCore> > aQCoreVector = make_unique<edm::DetSetVector<QCore> >();
    unique_ptr<edm::DetSetVector<ROCBitStream> > aBitStreamVector = make_unique<edm::DetSetVector<ROCBitStream> >();

    auto const &tTopo = iSetup.getData(tTopoToken_);

    edm::Handle<edm::DetSetVector<PixelDigi> > pixelDigiHandle;
    iEvent.getByToken(pixelDigi_token_, pixelDigiHandle);

    edm::DetSetVector<PixelDigi>::const_iterator iterDet;
    for ( iterDet = pixelDigiHandle->begin(); iterDet != pixelDigiHandle->end(); iterDet++ ) {

        DetId tkId = iterDet->id;
        edm::DetSet<PixelDigi> theDigis = (*pixelDigiHandle)[ tkId ];
        std::vector<Hit> hitlist;
        std::vector<int> id;

        if (tkId.subdetId() == PixelSubdetector::PixelBarrel) {
            int layer_num = tTopo.pxbLayer(tkId.rawId());
            int ladder_num = tTopo.pxbLadder(tkId.rawId());
            int module_num = tTopo.pxbModule(tkId.rawId());
            id = {tkId.subdetId(), layer_num, ladder_num, module_num};
        }
        else if (tkId.subdetId() == PixelSubdetector::PixelEndcap) {
            int module_num = tTopo.pxfModule(tkId());
            int disk_num = tTopo.pxfDisk(tkId());
            int blade_num = tTopo.pxfBlade(tkId());
            int panel_num = tTopo.pxfPanel(tkId());
            int side_num = tTopo.pxfSide(tkId());
            id = {tkId.subdetId(), module_num, disk_num, blade_num, panel_num, side_num};
        }

        for ( auto iterDigi = theDigis.begin(); iterDigi != theDigis.end(); ++iterDigi ) {
            hitlist.emplace_back(Hit(iterDigi->row(),iterDigi->column(),iterDigi->adc()));
        }

        std::vector<ReadoutChip> chips = processHits(hitlist);

        DetSet<QCore> DetSetQCores(tkId);
        DetSet<ROCBitStream> DetSetBitStream(tkId);

        for(size_t i = 0; i < chips.size(); i++) {
            ReadoutChip chip = chips[i];
            std::vector<QCore> qcores = chip.get_organized_QCores();
            for (auto& qcore:qcores) {
                DetSetQCores.push_back(qcore);
            }

            ROCBitStream aROCBitStream(i,chip.get_chip_code());
            DetSetBitStream.push_back(aROCBitStream);
        }  

        aBitStreamVector->insert(DetSetBitStream);
        aQCoreVector->insert(DetSetQCores);
    }

    iEvent.put( std::move(aQCoreVector) );
    iEvent.put( std::move(aBitStreamVector) );
}

void PixelQCoreProducer::beginJob(edm::EventSetup const&){}

void PixelQCoreProducer::endJob(){}

DEFINE_FWK_MODULE(PixelQCoreProducer);

