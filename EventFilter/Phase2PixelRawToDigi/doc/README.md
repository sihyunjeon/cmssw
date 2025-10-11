=== Compile & Run Instructions ===

NOTE : There is no compatible cabling map that can be used in CMSSW_15_0_0_pre2 for now. Though with same codes, everything runs under CMSSW_13_3_3 (borrowing
OTandITDTCCablingMap.db file from old geometry+cabling map combo).

```
cmsrel CMSSW_15_0_0_pre2
cd CMSSW_15_0_0_pre2/src
cmsenv
git cms-checkout-topic -u P2-Tracker-BES-SW:unpackers_15_0_0_pre2
scram b -j
cd EventFilter/Phase2PixelRawToDigi/test/
# Run IT Qcore --> RAW for Inner Tracker.
cmsRun Phase2PixelDigiToRaw_cfg.py
```

