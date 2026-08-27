// EDProducer that takes RawDataBuffer and fully decodes it straight to PixelDigi
// Fused unpacker: one pass over the same walk and decode as the split chain

#include <memory>
#include <vector>

#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/stream/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/Utilities/interface/InputTag.h"

#include "CondFormats/SiPhase2TrackerObjects/interface/TrackerDetToDTCELinkCablingMap.h"
#include "CondFormats/DataRecord/interface/TrackerDetToDTCELinkCablingMapRcd.h"

#include "DataFormats/Common/interface/DetSetVector.h"
#include "DataFormats/FEDRawData/interface/RawDataBuffer.h"
#include "DataFormats/SiPixelDigi/interface/PixelDigi.h"
#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITBitReader.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/Phase2DAQFormatSpecification.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/Phase2ITUnpacker.h"
#include "EventFilter/Phase2PixelRawToDigi/interface/SLinkModuleMap.h"

using namespace Phase2ITSpec;

class RawToPixelProducer : public edm::stream::EDProducer<> {
public:
  explicit RawToPixelProducer(const edm::ParameterSet&);
  ~RawToPixelProducer() override = default;

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions);
  void beginRun(const edm::Run&, const edm::EventSetup&) override;

private:
  void produce(edm::Event&, const edm::EventSetup&) override;

  const edm::EDGetTokenT<RawDataBuffer> fedRawDataToken_;
  // BeginRun copy builds the FED navigation; the per-event one supplies Module_SubType
  const edm::ESGetToken<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd> cablingMapBeginRunToken_;
  const edm::ESGetToken<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd> cablingMapToken_;
  // Must match the dropTot setting that produced the bitstream. When true the
  // encoded stream omits the per-hit 4-bit ToT field and every digi gets adc=0.
  const bool dropTot_;
  // Must match the encoder's handleGapPixels mode.
  const bool keepMode_;

  std::unique_ptr<SLinkModuleMap> slinkMap_;
};

RawToPixelProducer::RawToPixelProducer(const edm::ParameterSet& iConfig)
    : fedRawDataToken_(consumes<RawDataBuffer>(iConfig.getParameter<edm::InputTag>("fedRawDataCollection"))),
      cablingMapBeginRunToken_(
          esConsumes<TrackerDetToDTCELinkCablingMap, TrackerDetToDTCELinkCablingMapRcd, edm::Transition::BeginRun>()),
      cablingMapToken_(esConsumes()),
      dropTot_(iConfig.getParameter<bool>("dropTot")),
      keepMode_(
          Phase2ITUnpacker::parseKeepMode(iConfig.getParameter<std::string>("handleGapPixels"), "RawToPixelProducer")) {
  produces<edm::DetSetVector<PixelDigi>>();
}

void RawToPixelProducer::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<edm::InputTag>("fedRawDataCollection", edm::InputTag("rawDataCollector"));
  desc.add<bool>("dropTot", false);
  desc.add<std::string>("handleGapPixels", "DROP");
  descriptions.add("rawToPixelProducer", desc);
}

void RawToPixelProducer::beginRun(const edm::Run& iRun, const edm::EventSetup& iSetup) {
  slinkMap_ = std::make_unique<SLinkModuleMap>(iSetup.getData(cablingMapBeginRunToken_));
}

void RawToPixelProducer::produce(edm::Event& iEvent, const edm::EventSetup& iSetup) {
  auto outputPixelDigis = std::make_unique<edm::DetSetVector<PixelDigi>>();
  edm::Handle<RawDataBuffer> rawBuf;
  iEvent.getByToken(fedRawDataToken_, rawBuf);
  if (!rawBuf.isValid()) {
    throw cms::Exception("RawToPixelProducer") << "Invalid RawDataBuffer";
  }

  auto const& cablingMap = iSetup.getData(cablingMapToken_);

  for (const auto& entry : slinkMap_->fedIdToDetIds()) {
    int fedId = entry.first;
    auto frag = rawBuf->fragmentData(static_cast<uint32_t>(fedId));
    if (!frag.isValid()) {
      throw cms::Exception("RawToPixelProducer") << "Missing RawDataBuffer fragment for fed " << fedId
                                                 << ": cabling map lists this FED but the buffer has no source for it.";
    }
    auto fragSpan = frag.data();

    int fedSizeInWords = 0;
    const unsigned char* dataPtr =
        Phase2ITUnpacker::stripSLinkWrapper(fragSpan.data(), fragSpan.size(), fedId, fedSizeInWords);

    const std::vector<uint32_t>& detIds = slinkMap_->detIdsForFedId(fedId);
    if (!Phase2ITUnpacker::verifyHeaderTrailerPattern(dataPtr, 0)) {
      throw cms::Exception("RawToPixelProducer") << "Invalid header in FEDRawData";
    }
    const int trailerStart = Phase2ITUnpacker::findTrailerStart(dataPtr, fedSizeInWords);
    if (trailerStart < 0) {
      throw cms::Exception("RawToPixelProducer") << "Invalid trailer in FEDRawData";
    }

    Phase2ITUnpacker::forEachModule(
        dataPtr, fedSizeInWords, trailerStart, detIds.size(), [&](int modIdx, Phase2ITUnpacker::ModuleSpan span) {
          const uint32_t detId = detIds[modIdx];
          edm::DetSet<PixelDigi> moduleDigis(detId);
          // subtype is resolved only for modules that actually carry chips, as in the split chain
          int subtype = -1;
          Phase2ITUnpacker::forEachChip(
              dataPtr, span, fedSizeInWords, [&](int chipId, int payloadStartWord, uint32_t nBits) {
                if (subtype < 0)
                  subtype = static_cast<int>(cablingMap.getModuleInfo(detId).subtype);
                // decode in place out of the FED buffer
                Phase2ITBitReader reader(dataPtr + payloadStartWord * BYTES_PER_WORD, nBits);
                Phase2ITUnpacker::decodeChip(reader, chipId, subtype, dropTot_, keepMode_, moduleDigis);
              });
          if (!moduleDigis.empty())
            outputPixelDigis->insert(moduleDigis);
        });
  }

  iEvent.put(std::move(outputPixelDigis));
}

DEFINE_FWK_MODULE(RawToPixelProducer);
