// SPDX-FileCopyrightText: Intel Corporation
//
// SPDX-License-Identifier: BSD-3-Clause

#pragma once

#ifdef SYCL_LANGUAGE_VERSION
#include <oneapi/dpl/algorithm>
#include <oneapi/dpl/execution>
#include <oneapi/dpl/numeric>
#endif

#include <cassert>
#include <concepts>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#ifdef DR_FORMAT
#include <fmt/core.h>
#include <fmt/ranges.h>
#endif
// Workaround for doxygen warning about internal inconsistency
namespace fmt {}

#include "vendor/source_location/source_location.hpp"

// MPI should be optional
#include "mkl.h"
#include "mpi.h"

#include "range/v3/all.hpp"
namespace rng = ranges;

// clang-format off
#include "details/logger.hpp"
#include "details/communicator.hpp"
#include "concepts/concepts.hpp"
#include "details/view_detectors.hpp"
#include "details/segments_tools.hpp"
#include "details/views.hpp"
#include "details/memory.hpp"
#include "details/halo.hpp"
#include "mhp/cpos.hpp"
#include "mhp/containers/distributed_vector.hpp"
#include "mhp/views.hpp"
#include "mhp/algorithms/cpu_algorithms.hpp"
