#ifndef EventFilter_Phase2PixelRawToDigi_interface_Phase2ITModuleMapRecord_h
#define EventFilter_Phase2PixelRawToDigi_interface_Phase2ITModuleMapRecord_h

#include "CondFormats/DataRecord/interface/TrackerDetToDTCELinkCablingMapRcd.h"
#include "FWCore/Framework/interface/DependentRecordImplementation.h"
#include "Geometry/Records/interface/TrackerDigiGeometryRecord.h"

// The IT module map is derived from the cabling map and the tracker geometry,
// so it is valid over the intersection of their intervals.
class Phase2ITModuleMapRecord
    : public edm::eventsetup::DependentRecordImplementation<
          Phase2ITModuleMapRecord,
          edm::mpl::Vector<TrackerDetToDTCELinkCablingMapRcd, TrackerDigiGeometryRecord>> {};

#endif  // EventFilter_Phase2PixelRawToDigi_interface_Phase2ITModuleMapRecord_h
