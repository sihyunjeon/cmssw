#ifndef EventFilter_Phase2PixelRawToDigi_interface_Phase2ITModuleMapRecord_h
#define EventFilter_Phase2PixelRawToDigi_interface_Phase2ITModuleMapRecord_h

#include "CondFormats/DataRecord/interface/TrackerDetToDTCELinkCablingMapRcd.h"
#include "FWCore/Framework/interface/DependentRecordImplementation.h"
#include "Geometry/Records/interface/TrackerDigiGeometryRecord.h"

// Create the cabling and geometry map once
class Phase2ITModuleMapRecord : public edm::eventsetup::DependentRecordImplementation<
                                    Phase2ITModuleMapRecord,
                                    edm::mpl::Vector<TrackerDetToDTCELinkCablingMapRcd, TrackerDigiGeometryRecord>> {};

#endif  // EventFilter_Phase2PixelRawToDigi_interface_Phase2ITModuleMapRecord_h
