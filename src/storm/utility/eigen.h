#pragma once

// Include this utility header so we can access utility function from Eigen.
#include "src/storm/utility/constants.h"

// Finally include the parts of Eigen we need.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-attributes"
#include <Eigen/Sparse>
#include <unsupported/Eigen/IterativeSolvers>
#pragma GCC diagnostic pop