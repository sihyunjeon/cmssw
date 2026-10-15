import FWCore.ParameterSet.Config as cms

# FIXME This has to be gone once the cabling map is in conddb
def customiseITDTCCablingMap(process,
                             connect = 'sqlite_fip:EventFilter/Phase2PixelRawToDigi/test/OTandITDTCCablingMap.db',
                             tag = 'DTCCablingMapProducerUserRun'):
    from CondCore.CondDB.CondDB_cfi import CondDB
    process.itDTCCablingMap = cms.ESSource('PoolDBESSource',
        CondDB.clone(connect = connect),
        toGet = cms.VPSet(cms.PSet(
            record = cms.string('TrackerDetToDTCELinkCablingMapRcd'),
            tag = cms.string(tag),
        )),
    )
    process.es_prefer_itDTCCablingMap = cms.ESPrefer('PoolDBESSource', 'itDTCCablingMap',
                                                     TrackerDetToDTCELinkCablingMapRcd = cms.vstring('TrackerDetToDTCELinkCablingMap'))
    return process
