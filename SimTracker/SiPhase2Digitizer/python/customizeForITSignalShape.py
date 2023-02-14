import FWCore.ParameterSet.Config as cms

#
# activate signal shape in IT only
#

def customizeSiPhase2ITSignalShape(process):
    ## for standard mixing
    if hasattr(process,'mix') and hasattr(process.mix,'digitizers') and hasattr(process.mix.digitizers,'pixel'): 
        if hasattr(process.mix.digitizers.pixel,'PixelDigitizerAlgorithm'):
            print("# Activating signal shape emulation in IT pixel (planar)")
            process.mix.digitizers.pixel.PixelDigitizerAlgorithm.ApplyTimewalk = cms.bool(True)

    ## for pre-mixing
    if hasattr(process, "mixData") and hasattr(process.mixData, "workers") and hasattr(process.mixData.workers, "pixel"):
        if hasattr(process.mixData.workers.pixel,'PixelDigitizerAlgorithm'):
            print("# Activating signal shape emulation in IT pixel (planar)")
            process.mixData.workers.pixel.PixelDigitizerAlgorithm.ApplyTimewalk = cms.bool(True)

    return process

def customizeSiPhase2ITSignalShape12p5Nanoseconds(process):
    ## for standard mixing
    if hasattr(process,'mix') and hasattr(process.mix,'digitizers') and hasattr(process.mix.digitizers,'pixel'):
        if hasattr(process.mix.digitizers.pixel,'PixelDigitizerAlgorithm'):
            print("# Activating signal shape emulation in IT pixel (planar)")
            process.mix.digitizers.pixel.PixelDigitizerAlgorithm.ApplyTimewalk = cms.bool(True)
            process.mix.digitizers.pixel.PixelDigitizerAlgorithm.TofUpperCut = cms.double(12.5)
            process.mix.digitizers.pixel.PixelDigitizerAlgorithm.TofLowerCut = cms.double(-12.5)

    ## for pre-mixing
    if hasattr(process, "mixData") and hasattr(process.mixData, "workers") and hasattr(process.mixData.workers, "pixel"):
        if hasattr(process.mixData.workers.pixel,'PixelDigitizerAlgorithm'):
            print("# Activating signal shape emulation in IT pixel (planar)")
            process.mixData.workers.pixel.PixelDigitizerAlgorithm.ApplyTimewalk = cms.bool(True)
            process.mix.digitizers.pixel.PixelDigitizerAlgorithm.TofUpperCut = cms.double(12.5)
            process.mix.digitizers.pixel.PixelDigitizerAlgorithm.TofLowerCut = cms.double(-12.5)

    return process

def customizeSiPhase2ITSignalShape37p5Nanoseconds(process):
    ## for standard mixing
    if hasattr(process,'mix') and hasattr(process.mix,'digitizers') and hasattr(process.mix.digitizers,'pixel'):
        if hasattr(process.mix.digitizers.pixel,'PixelDigitizerAlgorithm'):
            print("# Activating signal shape emulation in IT pixel (planar)")
            process.mix.digitizers.pixel.PixelDigitizerAlgorithm.ApplyTimewalk = cms.bool(True)
            process.mix.digitizers.pixel.PixelDigitizerAlgorithm.TofUpperCut = cms.double(37.5)
            process.mix.digitizers.pixel.PixelDigitizerAlgorithm.TofLowerCut = cms.double(-37.5)

    ## for pre-mixing
    if hasattr(process, "mixData") and hasattr(process.mixData, "workers") and hasattr(process.mixData.workers, "pixel"):
        if hasattr(process.mixData.workers.pixel,'PixelDigitizerAlgorithm'):
            print("# Activating signal shape emulation in IT pixel (planar)")
            process.mixData.workers.pixel.PixelDigitizerAlgorithm.ApplyTimewalk = cms.bool(True)
            process.mix.digitizers.pixel.PixelDigitizerAlgorithm.TofUpperCut = cms.double(37.5)
            process.mix.digitizers.pixel.PixelDigitizerAlgorithm.TofLowerCut = cms.double(-37.5)

    return process

def customizeSiPhase2ITSignalShape62p5Nanoseconds(process):
    ## for standard mixing
    if hasattr(process,'mix') and hasattr(process.mix,'digitizers') and hasattr(process.mix.digitizers,'pixel'):
        if hasattr(process.mix.digitizers.pixel,'PixelDigitizerAlgorithm'):
            print("# Activating signal shape emulation in IT pixel (planar)")
            process.mix.digitizers.pixel.PixelDigitizerAlgorithm.ApplyTimewalk = cms.bool(True)
            process.mix.digitizers.pixel.PixelDigitizerAlgorithm.TofUpperCut = cms.double(62.5)
            process.mix.digitizers.pixel.PixelDigitizerAlgorithm.TofLowerCut = cms.double(-62.5)

    ## for pre-mixing
    if hasattr(process, "mixData") and hasattr(process.mixData, "workers") and hasattr(process.mixData.workers, "pixel"):
        if hasattr(process.mixData.workers.pixel,'PixelDigitizerAlgorithm'):
            print("# Activating signal shape emulation in IT pixel (planar)")
            process.mixData.workers.pixel.PixelDigitizerAlgorithm.ApplyTimewalk = cms.bool(True)
            process.mix.digitizers.pixel.PixelDigitizerAlgorithm.TofUpperCut = cms.double(62.5)
            process.mix.digitizers.pixel.PixelDigitizerAlgorithm.TofLowerCut = cms.double(-62.5)

    return process

def customizeSiPhase2ITSignalShape87p5Nanoseconds(process):
    ## for standard mixing
    if hasattr(process,'mix') and hasattr(process.mix,'digitizers') and hasattr(process.mix.digitizers,'pixel'):
        if hasattr(process.mix.digitizers.pixel,'PixelDigitizerAlgorithm'):
            print("# Activating signal shape emulation in IT pixel (planar)")
            process.mix.digitizers.pixel.PixelDigitizerAlgorithm.ApplyTimewalk = cms.bool(True)
            process.mix.digitizers.pixel.PixelDigitizerAlgorithm.TofUpperCut = cms.double(87.5)
            process.mix.digitizers.pixel.PixelDigitizerAlgorithm.TofLowerCut = cms.double(-87.5)

    ## for pre-mixing
    if hasattr(process, "mixData") and hasattr(process.mixData, "workers") and hasattr(process.mixData.workers, "pixel"):
        if hasattr(process.mixData.workers.pixel,'PixelDigitizerAlgorithm'):
            print("# Activating signal shape emulation in IT pixel (planar)")
            process.mixData.workers.pixel.PixelDigitizerAlgorithm.ApplyTimewalk = cms.bool(True)
            process.mix.digitizers.pixel.PixelDigitizerAlgorithm.TofUpperCut = cms.double(87.5)
            process.mix.digitizers.pixel.PixelDigitizerAlgorithm.TofLowerCut = cms.double(-87.5)

    return process

