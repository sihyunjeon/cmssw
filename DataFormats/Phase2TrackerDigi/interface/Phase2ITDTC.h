#ifndef PHASE2ITDTC_H
#define PHASE2ITDTC_H

#include "DataFormats/FEDRawData/interface/FEDRawDataCollection.h"
#include "DataFormats/FEDRawData/interface/FEDRawData.h"

class Phase2ITDTC{
  public:
    Phase2ITDTC(void): Phase2ITSLink(16), Phase2ITSLinkOffset(16), Phase2ITSLinkData(16) {};
    FEDRawData& getSLink(const unsigned int& index){ return Phase2ITSLink[index];}
    FEDRawData& getSLinkOffset(const unsigned int& index){ return Phase2ITSLinkOffset[index];}
    FEDRawData& getSLinkData(const unsigned int& index){ return Phase2ITSLinkData[index];}

  private:
    std::vector<FEDRawData> Phase2ITSLink;
    std::vector<FEDRawData> Phase2ITSLinkOffset;
    std::vector<FEDRawData> Phase2ITSLinkData;
};

#endif
