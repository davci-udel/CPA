set(cpa_source_files
	cpa.cpp
	cpaP.cpp
)

if (OPENCL_LIB AND OPENCL_INC)
	set(cpa_source_files ${cpa_source_files} opencl/pcpa-host.cpp)
endif (OPENCL_LIB AND OPENCL_INC)

if (CUDA_FOUND)
	add_subdirectory(cuda)
endif (CUDA_FOUND)

add_library(cpa ${cpa_source_files})

target_link_libraries(cpa common)

if (CUDA_FOUND)
	target_link_libraries(cpa cuda_cpa)
endif (CUDA_FOUND)
