import FWCore.ParameterSet.Config as cms

process = cms.Process("DTCCablingMapProducer")

process.load("FWCore.MessageLogger.MessageLogger_cfi")
process.load("CondCore.CondDB.CondDB_cfi")

# Output sqlite db
process.CondDB.connect = 'sqlite_file:OTandITDTCCablingMap.db'

process.source = cms.Source("EmptyIOVSource",
    timetype   = cms.string('runnumber'),
    firstValue = cms.uint64(1),
    lastValue  = cms.uint64(1),
    interval   = cms.uint64(1),
)

process.PoolDBOutputService = cms.Service("PoolDBOutputService",
    process.CondDB,
    timetype = cms.untracked.string('runnumber'),
    toPut    = cms.VPSet(cms.PSet(
        record = cms.string('TrackerDetToDTCELinkCablingMapRcd'),
        tag    = cms.string('DTCCablingMapProducerUserRun'),
    )),
)

# InnerTrackerModulesToDTCs.csv column layout (19 columns):
#   0  Module_DetId/i              6  N_Chips_Per_Module/I    12 LpGBT_Id/C
#   1  Module_SubType/I            7  N_Channels_Per_Module/I 13 LpGBT_CMSSW_IdPerDTC/U
#   2  Module_Section/C            8  Is_LongBarrel/O         14 MFB/I
#   3  Module_Layer/I              9  Power_Chain/I           15 DTC_Id/I
#   4  Module_Ring/I              10  Power_Chain_Type/C      16 DTC_CMSSW_Id/U
#   5  Module_phi_deg/D           11  N_ELinks_Per_Module/I   17 IsPlusZEnd/O
#                                                             18 IsPlusXSide/O
process.otdtccablingmap_producer = cms.EDAnalyzer("DTCCablingMapProducer",
    record = cms.string('TrackerDetToDTCELinkCablingMapRcd'),
    modulesToDTCCablingCSVFileNames = cms.vstring(
        "CondTools/SiPhase2Tracker/test/InnerTrackerModulesToDTCs.csv",
    ),
    csvFormat_ncolumns   = cms.uint32(19),
    csvFormat_idetid     = cms.uint32(0),    # Module_DetId
    csvFormat_idtcid     = cms.uint32(16),   # DTC_CMSSW_Id
    csvFormat_igbtlinkid = cms.uint32(13),   # LpGBT_CMSSW_IdPerDTC
    csvFormat_ielinkid   = cms.uint32(0),    # not in CSV -> dummy-fill
    dummy_fill_mode      = cms.string("DUMMY_FILL_ELINK_ID"),  # gbt from CSV, elink auto

    # per-module info
    read_innertracker_module_info = cms.bool(True),
    csvFormat_inchips  = cms.uint32(6),
    csvFormat_inelinks = cms.uint32(11),
    csvFormat_isection = cms.uint32(2),
    csvFormat_ilayer   = cms.uint32(3),
    csvFormat_iring    = cms.uint32(4),
    csvFormat_isubtype = cms.uint32(1),

    verbosity = cms.int32(0),
)

process.path = cms.Path(process.otdtccablingmap_producer)
