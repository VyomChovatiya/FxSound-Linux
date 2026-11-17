find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Threads REQUIRED)
execute_process(COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/linux/gen_include_shims.py ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/shims COMMAND_ERROR_IS_FATAL ANY)
add_library(original_fxdsp STATIC
  linux/dsp/../../dsp/DfxDsp.cpp
  linux/dsp/../../dsp/DfxDspEq.cpp
  linux/dsp/../../dsp/DfxDspPreset.cpp
  linux/dsp/../../dsp/DfxDspPrivate.cpp
  linux/dsp/../../dsp/DfxDspRegistry.cpp
  linux/dsp/../../dsp/ptComSftDfx/Comsftwr.c
  linux/dsp/../../dsp/ptComSftDfx/ComsftwrCPP.cpp
  linux/dsp/../../dsp/ptechDsp/Aural/Aural032/Auralp32.c
  linux/dsp/../../dsp/ptechDsp/Aural/Aural0/Auralp.c
  linux/dsp/../../dsp/ptechDsp/Dly32/Dly832/dly8p32.c
  linux/dsp/../../dsp/ptechDsp/Dly8/Dly8p.c
  linux/dsp/../../dsp/ptechDsp/Lex/Lex16/Lex16.c
  linux/dsp/../../dsp/ptechDsp/Lex/Lex32/Lex32.c
  linux/dsp/../../dsp/ptechDsp/Maximizer/Maxi16/Maxi16.c
  linux/dsp/../../dsp/ptechDsp/Maximizer/Maxi32/Maxi32.c
  linux/dsp/../../dsp/ptechDsp/Peq/Peq832/peq8p32.c
  linux/dsp/../../dsp/ptechDsp/Peq/Peq8/peq8p.c
  linux/dsp/../../dsp/ptechDsp/Play/Play16/Play16.c
  linux/dsp/../../dsp/ptechDsp/Play/Play32/Play32.c
  linux/dsp/../../dsp/ptechDsp/wide/Wide16/Wide16.c
  linux/dsp/../../dsp/ptechDsp/wide/Wide32/Wide32.c
  linux/dsp/../../dsp/ptutil/COM/Com.cpp
  linux/dsp/../../dsp/ptutil/COM/Comeprom.cpp
  linux/dsp/../../dsp/ptutil/COM/Comget.cpp
  linux/dsp/../../dsp/ptutil/COM/ComMem.cpp
  linux/dsp/../../dsp/ptutil/COM/Compass.cpp
  linux/dsp/../../dsp/ptutil/COM/Comread.cpp
  linux/dsp/../../dsp/ptutil/COM/Comwave.cpp
  linux/dsp/../../dsp/ptutil/COM/Comwrite.cpp
  linux/dsp/../../dsp/ptutil/dfxp/dfxpComm.cpp
  linux/dsp/../../dsp/ptutil/dfxp/dfxpEq.cpp
  linux/dsp/../../dsp/ptutil/dfxp/dfxpGet.cpp
  linux/dsp/../../dsp/ptutil/dfxp/dfxpInit.cpp
  linux/dsp/../../dsp/ptutil/dfxp/dfxpProcess.cpp
  linux/dsp/../../dsp/ptutil/dfxp/dfxpProcessClear.cpp
  linux/dsp/../../dsp/ptutil/dfxp/dfxpProcessInt.cpp
  linux/dsp/../../dsp/ptutil/dfxp/dfxpProcessReal.cpp
  linux/dsp/../../dsp/ptutil/dfxp/dfxpQnt.cpp
  linux/dsp/../../dsp/ptutil/dfxp/dfxpQuit.cpp
  linux/dsp/../../dsp/ptutil/dfxp/dfxpRegistryStandard.cpp
  linux/dsp/../../dsp/ptutil/dfxp/dfxpSession.cpp
  linux/dsp/../../dsp/ptutil/dfxp/dfxpSet.cpp
  linux/dsp/../../dsp/ptutil/dfxp/dfxpSpectrum.cpp
  linux/dsp/../../dsp/ptutil/dfxp/dfxpUniversal.cpp
  linux/dsp/../../dsp/ptutil/dfxSharedUtil/dfxSharedUtil.cpp
  linux/dsp/../../dsp/ptutil/DspUtil/BinauralSync/BinauralSynGet.cpp
  linux/dsp/../../dsp/ptutil/DspUtil/BinauralSync/BinauralSynInit.cpp
  linux/dsp/../../dsp/ptutil/DspUtil/BinauralSync/BinauralSynProcess.cpp
  linux/dsp/../../dsp/ptutil/DspUtil/BinauralSync/BinauralSynSet.cpp
  linux/dsp/../../dsp/ptutil/DspUtil/GraphicEq/GraphicEqGet.cpp
  linux/dsp/../../dsp/ptutil/DspUtil/GraphicEq/GraphicEqInit.cpp
  linux/dsp/../../dsp/ptutil/DspUtil/GraphicEq/GraphicEqInitBands.cpp
  linux/dsp/../../dsp/ptutil/DspUtil/GraphicEq/GraphicEqInitSections.cpp
  linux/dsp/../../dsp/ptutil/DspUtil/GraphicEq/GraphicEqProcess.cpp
  linux/dsp/../../dsp/ptutil/DspUtil/GraphicEq/GraphicEqSet.cpp
  linux/dsp/../../dsp/ptutil/DspUtil/spectrum/spectrumGet.cpp
  linux/dsp/../../dsp/ptutil/DspUtil/spectrum/spectrumInit.cpp
  linux/dsp/../../dsp/ptutil/DspUtil/spectrum/spectrumMessageValues.cpp
  linux/dsp/../../dsp/ptutil/DspUtil/spectrum/spectrumProcess.cpp
  linux/dsp/../../dsp/ptutil/DspUtil/spectrum/spectrumReset.cpp
  linux/dsp/../../dsp/ptutil/DspUtil/spectrum/spectrumSet.cpp
  linux/dsp/../../dsp/ptutil/DspUtil/SurroundSyn/SurroundSynInit.cpp
  linux/dsp/../../dsp/ptutil/DspUtil/SurroundSyn/SurroundSynProcess.cpp
  linux/dsp/../../dsp/ptutil/Filt/Fil12But.cpp
  linux/dsp/../../dsp/ptutil/Filt/FiltbiqdSos.cpp
  linux/dsp/../../dsp/ptutil/Filt/FiltCalcBiqd.cpp
  linux/dsp/../../dsp/ptutil/Filt/FiltCalcFilterResponse.cpp
  linux/dsp/../../dsp/ptutil/Filt/Filtpoly.cpp
  linux/dsp/../../dsp/ptutil/Filt/FiltRun.cpp
  linux/dsp/../../dsp/ptutil/PRELST/Prelst.cpp
  linux/dsp/../../dsp/ptutil/PWAV/PwavConvert.cpp
  linux/dsp/../../dsp/ptutil/PWAV/PwavUtil.cpp
  linux/dsp/../../dsp/ptutil/Qnt/Qnt.cpp
  linux/dsp/../../dsp/ptutil/Qnt/Qnt2But.cpp
  linux/dsp/../../dsp/ptutil/Qnt/QntitoBoostCut.cpp
  linux/dsp/../../dsp/ptutil/Qnt/Qntitol.cpp
  linux/dsp/../../dsp/ptutil/Qnt/Qntitor.cpp
  linux/dsp/../../dsp/ptutil/Qnt/Qntitor2.cpp
  linux/dsp/../../dsp/ptutil/Qnt/Qntrtoi.cpp
  linux/dsp/../../dsp/ptutil/Qnt/Qntrtol.cpp
  linux/dsp/../../dsp/ptutil/Qnt/Qntrtor.cpp
  linux/dsp/../../dsp/ptutil/realSample/realSampleForceLegalValues.cpp
  linux/dsp/../../dsp/ptutil/SOS/Sos.cpp
  linux/dsp/../../dsp/ptutil/SOS/SosGet.cpp
  linux/dsp/../../dsp/ptutil/SOS/SosProcess.cpp
  linux/dsp/../../dsp/ptutil/SOS/SosSet.cpp
  linux/dsp/../../dsp/ptutil/VALS/Vals.cpp
  linux/dsp/../../dsp/ptutil/VALS/Valscfg.cpp
  linux/dsp/../../dsp/ptutil/VALS/Valsfile.cpp
  linux/dsp/../../dsp/ptutil/VALS/Valsget.cpp
  linux/dsp/../../dsp/ptutil/VALS/Valsset.cpp
  linux/dsp/../../dsp/ptutil/VALS/Valswarp.cpp
  linux/dsp/../../audiopassthru/src/MTH/MthUtil.cpp
  linux/dsp/../../audiopassthru/src/MTH/Mthfreq.cpp
  linux/dsp/../../audiopassthru/src/MTH/MthBuffer.cpp
  linux/dsp/../../audiopassthru/src/SLOUT/Slout.cpp
  linux/dsp/../compat/fx_compat.cpp
)
target_include_directories(original_fxdsp PRIVATE dsp/ptutil/include dsp/include audiopassthru/include linux/compat ${CMAKE_CURRENT_BINARY_DIR}/shims)
target_compile_options(original_fxdsp PRIVATE -w -fno-strict-aliasing -fwrapv -include ${CMAKE_CURRENT_SOURCE_DIR}/linux/compat/fx_compat.h)
target_compile_definitions(original_fxdsp PRIVATE WIN32 PT_NON_MFC DSPSOFT_TARGET PT_DSP_BUILD=PT_DSP_DFX)
target_link_libraries(original_fxdsp PUBLIC Threads::Threads)
target_include_directories(original_fxdsp INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/dsp/include)
add_executable(original_dsp_selftest ../../tests/original_dsp_selftest.cpp)
target_link_libraries(original_dsp_selftest PRIVATE original_fxdsp)
