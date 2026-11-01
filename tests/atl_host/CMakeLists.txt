add_executable(openatl_activex tests.cpp lifecycle.cpp)
target_link_libraries(openatl_activex PRIVATE OpenWTL::WTL)
set_target_properties(openatl_activex PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
if(MINGW)
  target_link_options(openatl_activex PRIVATE -static)
endif()
foreach(mode IN ITEMS events browser lifecycle)
  add_test(NAME openatl.activex_${mode} COMMAND openatl_activex ${mode})
  set_tests_properties(openatl.activex_${mode} PROPERTIES TIMEOUT 25)
endforeach()

# Run the pinned original class sources inside the existing STA events case.
# No new executable, test mode, exports or external project are introduced.
if(CMAKE_HOST_WIN32)
  set(_commap_original "${PROJECT_SOURCE_DIR}/tests/official_samples/microsoft_atl/commap")
  set(_commap_generated "${PROJECT_BINARY_DIR}/tests/com_server/microsoft-commap")
  target_sources(openatl_activex PRIVATE
    "${_commap_original}/Outer.cpp" "${_commap_original}/Chain.cpp"
    "${_commap_original}/aggreg/Agg.cpp" "${_commap_original}/aggreg/AggBlind.cpp"
    "${_commap_original}/aggreg/AutoAgg.cpp" "${_commap_original}/aggreg/AutoAggB.cpp"
    "${_commap_original}/commap_classes_native.cpp"
    "${_commap_generated}/COMMap_i.c" "${_commap_generated}/aggreg/aggreg_i.c"
    "${PROJECT_BINARY_DIR}/tests/com_server/fixture_typelib.rc")
  set_source_files_properties("${_commap_generated}/COMMap_i.c"
    "${_commap_generated}/aggreg/aggreg_i.c" PROPERTIES GENERATED TRUE LANGUAGE CXX)
  target_include_directories(openatl_activex PRIVATE
    "${_commap_generated}" "${_commap_generated}/aggreg")
  # The original premap/preagg headers select apartment threading. Keep every
  # translation unit of this STA test binary on the same ATL type definitions.
  target_compile_definitions(openatl_activex PRIVATE
    _ATL_APARTMENT_THREADED= OPENATL_ORIGINAL_COMMAP_CLASSES=1)
  add_dependencies(openatl_activex openatl_microsoft_commap_midl)
endif()
