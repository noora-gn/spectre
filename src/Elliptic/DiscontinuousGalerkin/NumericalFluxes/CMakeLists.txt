# Distributed under the MIT License.
# See LICENSE.txt for details.

spectre_target_sources(
  EllipticDg
  PRIVATE
  FirstOrderInternalPenalty.cpp
  )

spectre_target_headers(
  EllipticDg
  INCLUDE_DIRECTORY ${CMAKE_SOURCE_DIR}/src
  HEADERS
  FirstOrderInternalPenalty.hpp
  )
