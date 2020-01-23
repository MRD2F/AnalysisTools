function(scram_AddLibAndInclude tool_name )
    execute_process(COMMAND scram tool info ${tool_name} OUTPUT_VARIABLE SCRAM_INFO)
    string(REGEX MATCH "\nINCLUDE=([^\n]*)" SCRAM_STR ${SCRAM_INFO})
    set(inc_dir "${CMAKE_MATCH_1}")
    if(inc_dir)
        set("${tool_name}_INCLUDE" "${inc_dir}" PARENT_SCOPE)
    endif()
    if(ARGV1)
        set(lib_dir "${ARGV1}")
    else()
        string(REGEX MATCH "\nLIBDIR=([^\n]*)" SCRAM_STR ${SCRAM_INFO})
        set(lib_dir "${CMAKE_MATCH_1}")
    endif()
    if(lib_dir)
        set("${tool_name}_LIBDIR" "${lib_dir}" PARENT_SCOPE)
        string(REGEX MATCH "\nLIB=([^\n]*)" SCRAM_STR ${SCRAM_INFO})
        set(lib_name "${CMAKE_MATCH_1}")
        if(lib_name)
            set("${tool_name}_LIB" "${lib_dir}/lib${lib_name}.so" PARENT_SCOPE)
        endif()
    endif()
endfunction(scram_AddLibAndInclude)

if(scram_path)
    scram_AddLibAndInclude(eigen)
    scram_AddLibAndInclude(protobuf)
    scram_AddLibAndInclude(tensorflow)
    scram_AddLibAndInclude(tensorflow-cc "${tensorflow_LIBDIR}")

    set(TF_INCLUDES ${eigen_INCLUDE} ${protobuf_INCLUDE} ${tensorflow_INCLUDE})
    set(TF_LIBRARIES ${protobuf_LIB} ${tensorflow-cc_LIB} $ENV{CMSSW_RELEASE_BASE}/lib/$ENV{SCRAM_ARCH}/libPhysicsToolsTensorFlow.so)
else()
    message(FATAL_ERROR "Standalone tensorflow support is not implemented")
endif()