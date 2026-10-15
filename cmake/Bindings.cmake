# Geode normally tracks bindings/main. Pin the actual API used by this adapter.
include(FetchContent)
FetchContent_Declare(routefinder_bindings
    URL https://codeload.github.com/geode-sdk/bindings/tar.gz/f8aec5d1f3df76d840dfd2609f02cef30dc6efdf
    URL_HASH SHA256=e339ac58d192bc8948e40923bc218338f1d55d45417ece7286481a36495cadff
    SOURCE_SUBDIR __download_only__)
FetchContent_GetProperties(routefinder_bindings)
if(NOT routefinder_bindings_POPULATED)
    FetchContent_MakeAvailable(routefinder_bindings)
endif()
set(GEODE_BINDINGS_REPO_PATH "${routefinder_bindings_SOURCE_DIR}" CACHE PATH "Pinned bindings" FORCE)
