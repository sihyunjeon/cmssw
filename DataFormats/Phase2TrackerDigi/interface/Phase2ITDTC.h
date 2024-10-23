#ifndef PHASE2ITDTC_H
#define PHASE2ITDTC_H

#include "DataFormats/FEDRawData/interface/FEDRawDataCollection.h"
#include "DataFormats/FEDRawData/interface/FEDRawData.h"

class Phase2ITDTC{
  public:
    Phase2ITDTC(void): Phase2ITSLink(16) {};
    FEDRawData& getSLink(const unsigned int& index){ return Phase2ITSLink[index];}

  private:
    std::vector<FEDRawData> Phase2ITSLink;
};

#endif
