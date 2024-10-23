#ifndef PHASE2ITDTCCOLLECTION_H
#define PHASE2ITDTCCOLLECTION_H

#include "DataFormats/FEDRawData/interface/FEDRawDataCollection.h"
#include "DataFormats/FEDRawData/interface/FEDRawData.h"

#include "DataFormats/Phase2TrackerDigi/interface/Phase2ITDTC.h"

class Phase2ITDTCCollection{
  public:
    Phase2ITDTCCollection(unsigned int& event): DTC(36), EventID(event) {};
    Phase2ITDTC& getDTC(const unsigned int& index){ return DTC[index];}

  private:
    std::vector<Phase2ITDTC> DTC;
    unsigned int EventID;
};

#endif
