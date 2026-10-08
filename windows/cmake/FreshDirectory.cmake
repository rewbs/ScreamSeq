# Test fixture: replace DIRECTORY with an empty directory. Worker tests save
# with overwrite:false, so each run needs private scratch storage. Only paths
# inside the build tree's test-scratch directory are accepted.
if(NOT DEFINED DIRECTORY OR NOT IS_ABSOLUTE "${DIRECTORY}" OR NOT DIRECTORY MATCHES "/test-scratch/[^/]+$")
  message(FATAL_ERROR "FreshDirectory requires an absolute <build>/test-scratch/<name> path")
endif()
file(REMOVE_RECURSE "${DIRECTORY}")
file(MAKE_DIRECTORY "${DIRECTORY}")
