# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/home/linux-user/Documents/Stack-Booster/build/_deps/inih-src")
  file(MAKE_DIRECTORY "/home/linux-user/Documents/Stack-Booster/build/_deps/inih-src")
endif()
file(MAKE_DIRECTORY
  "/home/linux-user/Documents/Stack-Booster/build/_deps/inih-build"
  "/home/linux-user/Documents/Stack-Booster/build/_deps/inih-subbuild/inih-populate-prefix"
  "/home/linux-user/Documents/Stack-Booster/build/_deps/inih-subbuild/inih-populate-prefix/tmp"
  "/home/linux-user/Documents/Stack-Booster/build/_deps/inih-subbuild/inih-populate-prefix/src/inih-populate-stamp"
  "/home/linux-user/Documents/Stack-Booster/build/_deps/inih-subbuild/inih-populate-prefix/src"
  "/home/linux-user/Documents/Stack-Booster/build/_deps/inih-subbuild/inih-populate-prefix/src/inih-populate-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/home/linux-user/Documents/Stack-Booster/build/_deps/inih-subbuild/inih-populate-prefix/src/inih-populate-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/home/linux-user/Documents/Stack-Booster/build/_deps/inih-subbuild/inih-populate-prefix/src/inih-populate-stamp${cfgdir}") # cfgdir has leading slash
endif()
