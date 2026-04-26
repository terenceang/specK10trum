set(COMPONENT_SRCS "raw_stream.c")
set(COMPONENT_ADD_INCLUDEDIRS "include")

if("${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}" VERSION_LESS "5.0")
    list(APPEND COMPONENT_SRCS  "i2s_stream.c")
else()
    list(APPEND COMPONENT_SRCS  "i2s_stream_idf5.c")
endif()

set(COMPONENT_REQUIRES audio_pipeline driver audio_sal esp-adf-libs)

register_component()
