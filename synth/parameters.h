#pragma once

#include "inputs.h"
// Standard C includes
#include <cmath>

// Toggle STDP
//#define STDP

//------------------------------------------------------------------------
// Parameters
//------------------------------------------------------------------------
namespace Parameters
{
    const double timestep = 0.1;

    const double resetVoltage = 0.0;
    const double thresholdVoltage = 20.0;

    // Number of cells
    const unsigned int numNeurons = (int) std::sqrt(NSYN / 0.00156);//10000;

    const unsigned int numTimesteps = 10000;//10000;

    // connection probability
    const double probabilityConnection = 0.00156;

	// Rate of Poisson noise injected into each neuron (Hz)
    const double inputRate = 50.0;

    const double excitatoryWeight = 0.1;

    // Axonal delay
    const double delayMs = 0.1;
    const unsigned int delayTimesteps = (unsigned int)std::round(delayMs / timestep);

}