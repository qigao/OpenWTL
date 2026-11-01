# Build actual type REGISTRY Win32 resource blobs. The two ordinary
# fixture scripts match the test COM CLSIDs; the other two files are the
# unchanged, checksum-pinned Microsoft ATL RGS resources from #30.
file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}/tests/com_server/rgs_value.rgs"
  _openatl_fixture_value_rgs)
file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}/tests/com_server/rgs_aux.rgs"
  _openatl_fixture_aux_rgs)
file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}/tests/atl_rgs/fixtures/Chain.rgs"
  _openatl_fixture_chain_rgs)
file(TO_CMAKE_PATH "${PROJECT_SOURCE_DIR}/tests/atl_rgs/fixtures/Outer.rgs"
  _openatl_fixture_outer_rgs)
set(_openatl_fixture_rc "${CMAKE_CURRENT_BINARY_DIR}/fixture_registry.rc")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/fixture_registry.rc.in"
  "${_openatl_fixture_rc}" @ONLY)
add_library(openatl_com_server_fixture SHARED
  fixture.cpp embedded_dispatch.cpp fixture.def fixture_registry_ids.h
  "${_openatl_fixture_rc}" interfaces.h)
set_target_properties(openatl_com_server_fixture PROPERTIES PREFIX "")
target_link_libraries(openatl_com_server_fixture PRIVATE OpenWTL::WTL)

# Four unchanged, pinned Microsoft ATL RGS samples: verify their original
# Git blobs at CMake configure time and embed their bytes in Windows-native
# tests. No registry writes or RGS execution is done by the parser.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
set(_rgs_src "${PROJECT_SOURCE_DIR}/tests/atl_rgs")
set(_rgs_fixtures
  "${_rgs_src}/fixtures/Chain.rgs"
  "${_rgs_src}/fixtures/Outer.rgs"
  "${_rgs_src}/fixtures/ArrayManager.rgs"
  "${_rgs_src}/fixtures/MapCollection.rgs")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_rgs_fixtures})
execute_process(COMMAND "${Python3_EXECUTABLE}"
  "${_rgs_src}/embed_fixtures.py"
  "${CMAKE_CURRENT_BINARY_DIR}/openatl_rgs_fixtures.generated.h"
  "${_rgs_src}/fixtures"
  COMMAND_ERROR_IS_FATAL ANY)

add_executable(openatl_com_server_tests
  server.cpp atl_compat.cpp atl_module.cpp atl_dispatch.cpp
  atl_typelib_file.cpp rgs_parser_tests.cpp rgs_registry_tests.cpp interfaces.h
  "${_openatl_fixture_rc}")
target_include_directories(openatl_com_server_tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
# Inline RGS registration functions must have identical definitions in
# EVERY translation unit of the test executable. Otherwise the test-only
# fault hook can be optimized out by a differently compiled inline copy.
# This define applies only to the native TEST executable, never the DLL.
target_compile_definitions(openatl_com_server_tests PRIVATE OPENATL_RGS_TEST_FAULT=1)
target_link_libraries(openatl_com_server_tests PRIVATE OpenWTL::WTL)
add_dependencies(openatl_com_server_tests openatl_com_server_fixture)
set_target_properties(openatl_com_server_fixture openatl_com_server_tests PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")


# Exact Microsoft VCSamples COMMap.idl: SHA-pinned and unchanged.
# The real native Windows SDK midl.exe supplies .h/.c/.tlb; no Mono/Wine
# or synthetic interfaces and no automatic regsvr32/TypeLib registration.
set(_openatl_commap_original
  "${PROJECT_SOURCE_DIR}/tests/official_samples/microsoft_atl/commap")
set(_openatl_commap_idl "${_openatl_commap_original}/COMMap.idl")
# Keep every immutable source/RGS blob on the incremental configure graph.
file(READ "${_openatl_commap_original}/original_sources.json" _commap_snapshot)
string(JSON _commap_blob_count LENGTH "${_commap_snapshot}" files)
math(EXPR _commap_last_blob "${_commap_blob_count} - 1")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${_openatl_commap_original}/original_sources.json")
foreach(_index RANGE 0 ${_commap_last_blob})
  string(JSON _source MEMBER "${_commap_snapshot}" files ${_index})
  # Canonical fixtures also occur in _rgs_fixtures; normalize ../ aliases so
  # Ninja sees each configure input once instead of duplicate output paths.
  get_filename_component(_source_path "${_openatl_commap_original}/${_source}" ABSOLUTE)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_source_path}")
endforeach()
execute_process(COMMAND "${Python3_EXECUTABLE}"
  "${_openatl_commap_original}/check_idl.py" "${_openatl_commap_idl}"
  COMMAND_ERROR_IS_FATAL ANY)
if(CMAKE_HOST_WIN32)
  find_program(OPENATL_COMMAP_MIDL_EXECUTABLE NAMES midl.exe midl
    HINTS "$ENV{OPENATL_WINDOWS_SDK_BIN}"
      "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/x86"
      "$ENV{WindowsSdkDir}/bin/$ENV{WindowsSDKVersion}/x64"
    REQUIRED)
  get_filename_component(_openatl_midl_arch_bin
    "${OPENATL_COMMAP_MIDL_EXECUTABLE}" DIRECTORY)
  get_filename_component(_openatl_midl_sdk_bin
    "${_openatl_midl_arch_bin}" DIRECTORY)
  get_filename_component(_openatl_midl_sdk_version
    "${_openatl_midl_sdk_bin}" NAME)
  get_filename_component(_openatl_midl_sdk_bin_root
    "${_openatl_midl_sdk_bin}" DIRECTORY)
  get_filename_component(_openatl_midl_sdk_root
    "${_openatl_midl_sdk_bin_root}" DIRECTORY)
  # MIDL defaults to cl.exe, absent in a native llvm-mingw build.
  # The pinned llvm-mingw package's clang.exe is a GNU-style driver even
  # when hard-linked under the clang-cl.exe name. Use documented MIDL
  # /cpp_cmd + /cpp_opt to explicitly select GNU preprocessing. Preserve
  # all SDK IDL preprocessor directives (do not use /no_cpp).
  set(_openatl_midl_cpp_args)
  set(_openatl_midl_env win32)
  set(_openatl_midl_cpp_target i686-w64-mingw32)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    # Automation TypeLibs distinguish Win32 and Win64 pointer/vtable widths.
    # Both supported 64-bit Windows CPUs use SYS_WIN64 TypeLib metadata.
    set(_openatl_midl_env amd64)
    set(_openatl_midl_cpp_target x86_64-w64-mingw32)
  endif()
  if(MINGW)
    find_program(OPENATL_COMMAP_PINNED_CLANG_EXECUTABLE NAMES clang.exe
      HINTS "$ENV{OPENATL_LLVM_MINGW_BIN}" NO_DEFAULT_PATH REQUIRED)
  endif()
  set(_openatl_midl_include_flags)
  set(_openatl_midl_rsp "")
  if(MINGW)
    set(_openatl_midl_rsp "${CMAKE_CURRENT_BINARY_DIR}/commap-llvm-cpp.rsp")
    file(WRITE "${_openatl_midl_rsp}" "")
  endif()
  foreach(_atl_folder IN ITEMS um shared)
    set(_atl_sdk_idl_include
      "${_openatl_midl_sdk_root}/Include/${_openatl_midl_sdk_version}/${_atl_folder}")
    if(IS_DIRECTORY "${_atl_sdk_idl_include}")
      list(APPEND _openatl_midl_include_flags "/I" "${_atl_sdk_idl_include}")
      if(MINGW)
        file(APPEND "${_openatl_midl_rsp}" "-I\"${_atl_sdk_idl_include}\"\n")
      endif()
    endif()
  endforeach()
  if(MINGW)
    # Clang reads precisely the UTF-8 response-file SDK include paths,
    # including spaces in Program Files (x86); MIDL still gets /I itself.
    # MIDL expects literal __stdcall/__cdecl tokens in SDK IDL, whereas
    # llvm-mingw's GNU-mode Clang predefines them as GNU __attribute__(())
    # macros. Undefine those ABI sugar macros, retain real IDL declarations,
    # and match the target TypeLib's pointer width.
    set(_openatl_midl_cpp_options
      "-E -x c --target=${_openatl_midl_cpp_target} -U__stdcall -U__cdecl -U__fastcall -U_stdcall -U_cdecl -U_fastcall @${_openatl_midl_rsp}")
    list(APPEND _openatl_midl_cpp_args
      /cpp_cmd "${OPENATL_COMMAP_PINNED_CLANG_EXECUTABLE}"
      /cpp_opt "${_openatl_midl_cpp_options}")
  endif()
  set(_openatl_commap_generated "${CMAKE_CURRENT_BINARY_DIR}/microsoft-commap")
  file(MAKE_DIRECTORY "${_openatl_commap_generated}")
  set(_openatl_commap_header "${_openatl_commap_generated}/COMMap.h")
  set(_openatl_commap_iid "${_openatl_commap_generated}/COMMap_i.c")
  set(_openatl_commap_tlb "${_openatl_commap_generated}/COMMap.tlb")
  add_custom_command(
    OUTPUT "${_openatl_commap_header}" "${_openatl_commap_iid}"
           "${_openatl_commap_tlb}"
    COMMAND "${OPENATL_COMMAP_MIDL_EXECUTABLE}" /nologo /env ${_openatl_midl_env}
      ${_openatl_midl_cpp_args} ${_openatl_midl_include_flags}
      /out "${_openatl_commap_generated}" /h COMMap.h
      /iid COMMap_i.c /tlb COMMap.tlb "${_openatl_commap_idl}"
    COMMAND "${CMAKE_COMMAND}" "-DOPENATL_MIDL_MINGW=${MINGW}"
      "-DOPENATL_MIDL_HEADER=${_openatl_commap_header}"
      -P "${PROJECT_SOURCE_DIR}/cmake/OpenATLMidlUUID.cmake"
    DEPENDS "${_openatl_commap_idl}"
      "${PROJECT_SOURCE_DIR}/cmake/OpenATLMidlUUID.cmake"
      "${PROJECT_SOURCE_DIR}/cmake/OpenATLRibbon.cmake"
    COMMENT "Actual Windows SDK MIDL: original Microsoft COMMap.idl"
    VERBATIM)
  # The original COuter includes and activates the sample's four aggregate
  # classes. Generate their real declarations/IIDs from unchanged Aggreg.idl.
  set(_openatl_aggreg_generated "${_openatl_commap_generated}/aggreg")
  file(MAKE_DIRECTORY "${_openatl_aggreg_generated}")
  set(_openatl_aggreg_idl "${_openatl_commap_original}/aggreg/Aggreg.idl")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_openatl_commap_original}/original_sources.json" "${_openatl_aggreg_idl}")
  add_custom_command(
    OUTPUT "${_openatl_aggreg_generated}/Aggreg.h"
           "${_openatl_aggreg_generated}/aggreg_i.c"
           "${_openatl_aggreg_generated}/Aggreg.tlb"
    COMMAND "${OPENATL_COMMAP_MIDL_EXECUTABLE}" /nologo /env ${_openatl_midl_env}
      ${_openatl_midl_cpp_args} ${_openatl_midl_include_flags}
      /out "${_openatl_aggreg_generated}" /h Aggreg.h
      /iid aggreg_i.c /tlb Aggreg.tlb "${_openatl_aggreg_idl}"
    COMMAND "${CMAKE_COMMAND}" "-DOPENATL_MIDL_MINGW=${MINGW}"
      "-DOPENATL_MIDL_HEADER=${_openatl_aggreg_generated}/Aggreg.h"
      -P "${PROJECT_SOURCE_DIR}/cmake/OpenATLMidlUUID.cmake"
    DEPENDS "${_openatl_aggreg_idl}"
      "${PROJECT_SOURCE_DIR}/cmake/OpenATLMidlUUID.cmake"
      "${PROJECT_SOURCE_DIR}/cmake/OpenATLRibbon.cmake"
    COMMENT "Actual Windows SDK MIDL: original Microsoft Aggreg.idl"
    VERBATIM)
  add_custom_target(openatl_microsoft_commap_midl ALL DEPENDS
    "${_openatl_commap_header}" "${_openatl_commap_iid}"
    "${_openatl_commap_tlb}"
    "${_openatl_aggreg_generated}/Aggreg.h"
    "${_openatl_aggreg_generated}/aggreg_i.c"
    "${_openatl_aggreg_generated}/Aggreg.tlb")
  add_dependencies(openatl_com_server_tests openatl_microsoft_commap_midl)
  set_source_files_properties("${_openatl_commap_iid}"
    PROPERTIES GENERATED TRUE LANGUAGE CXX)
  target_sources(openatl_com_server_tests PRIVATE
    "${_openatl_commap_iid}"
    "${_openatl_commap_original}/commap_midl_native.cpp")
  target_include_directories(openatl_com_server_tests PRIVATE
    "${_openatl_commap_generated}")
  target_compile_definitions(openatl_com_server_tests PRIVATE
    OPENATL_ORIGINAL_COMMAP_MIDL=1)
  # Embed the same real MIDL output in both existing native test images.
  # Target ordering makes the TypeLib available before Windows RC runs.
  set(_openatl_embedded_rc "${CMAKE_CURRENT_BINARY_DIR}/fixture_typelib.rc")
  file(WRITE "${_openatl_embedded_rc}"
    "1 TYPELIB \"${_openatl_commap_tlb}\"\n2 TYPELIB \"${_openatl_aggreg_generated}/Aggreg.tlb\"\n")
  target_sources(openatl_com_server_fixture PRIVATE
    "${_openatl_embedded_rc}" "${_openatl_commap_iid}")
  target_sources(openatl_com_server_tests PRIVATE
    "${_openatl_embedded_rc}" embedded_dispatch.cpp)
  set_source_files_properties("${_openatl_embedded_rc}" PROPERTIES
    OBJECT_DEPENDS "${_openatl_commap_tlb};${_openatl_aggreg_generated}/Aggreg.tlb")
  add_dependencies(openatl_com_server_fixture openatl_microsoft_commap_midl)
  target_include_directories(openatl_com_server_fixture PRIVATE
    "${_openatl_commap_generated}")
  target_compile_definitions(openatl_com_server_fixture PRIVATE
    OPENATL_ORIGINAL_COMMAP_MIDL=1)
  # Admit the original two DLL entry points/resources inside existing DLLs.
  # The original .cpp files include their MIDL IID sources themselves.
  foreach(_pair IN ITEMS commap aggreg)
    if(_pair STREQUAL "commap")
      set(_image openatl_unload_fixture)
      set(_source "${_openatl_commap_original}")
      set(_generated "${_openatl_commap_generated}")
      set(_module COMMap)
      set(_own_exports "${PROJECT_SOURCE_DIR}/tests/wtl10/unload_fixture.def")
      set(_classes Outer.cpp Chain.cpp)
      foreach(_rgs IN ITEMS Outer Chain)
        configure_file("${PROJECT_SOURCE_DIR}/tests/atl_rgs/fixtures/${_rgs}.rgs"
          "${_generated}/${_rgs}.rgs" COPYONLY)
      endforeach()
    else()
      set(_image openatl_unload_probe)
      set(_source "${_openatl_commap_original}/aggreg")
      set(_generated "${_openatl_aggreg_generated}")
      set(_module Aggreg)
      set(_own_exports "${PROJECT_SOURCE_DIR}/tests/wtl10/unload_probe.def")
      set(_classes Agg.cpp AggBlind.cpp AutoAgg.cpp AutoAggB.cpp)
      foreach(_rgs IN ITEMS Agg AggBlind AutoAgg AutoAggB)
        configure_file("${_source}/${_rgs}.rgs" "${_generated}/${_rgs}.rgs" COPYONLY)
      endforeach()
    endif()
    configure_file("${_source}/${_module}.rc" "${_generated}/${_module}.rc" COPYONLY)
    configure_file("${_source}/resource.h" "${_generated}/resource.h" COPYONLY)
    target_sources(${_image} PRIVATE "${_source}/${_module}.cpp"
      "${_generated}/${_module}.rc" "${_openatl_commap_original}/dll_per_user.cpp")
    foreach(_class IN LISTS _classes)
      target_sources(${_image} PRIVATE "${_source}/${_class}")
    endforeach()
    target_include_directories(${_image} PRIVATE "${_generated}"
      "${_openatl_commap_generated}" "${_openatl_commap_original}")
    target_compile_definitions(${_image} PRIVATE _ATL_APARTMENT_THREADED=)
    set_source_files_properties("${_generated}/${_module}.rc"
      TARGET_DIRECTORY ${_image} PROPERTIES OBJECT_DEPENDS "${_generated}/${_module}.tlb")
    add_dependencies(${_image} openatl_microsoft_commap_midl)
    # Keep every original private COM export and every existing GUI export.
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_own_exports}")
    file(READ "${_own_exports}" _exports)
    file(READ "${_source}/${_module}.def" _original_exports)
    string(FIND "${_original_exports}" "EXPORTS" _export_start)
    math(EXPR _export_start "${_export_start} + 7")
    string(SUBSTRING "${_original_exports}" ${_export_start} -1 _com_exports)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/original_${_pair}_exports.def"
      "${_exports}\n${_com_exports}")
  endforeach()
  add_test(NAME openatl.microsoft_commap_midl
    COMMAND openatl_com_server_tests --commap-tlb "${_openatl_commap_tlb}")
  set_tests_properties(openatl.microsoft_commap_midl PROPERTIES TIMEOUT 30)
endif()

foreach(mode IN ITEMS core sta mta module)
  add_test(NAME openatl.com_server_${mode} COMMAND openatl_com_server_tests --${mode})
  set_tests_properties(openatl.com_server_${mode} PROPERTIES
    TIMEOUT 30 RESOURCE_LOCK openatl_com_server_registration)
endforeach()
