#pragma once

#include "inputs.h"
// Standard C includes
#include <cmath>

//------------------------------------------------------------------------
// Parameters
//------------------------------------------------------------------------
namespace Parameters
{
    const double timestep = 0.1;

    const double resetVoltage = -60.0;
    const double restVoltage = -60.0;
    const double thresholdVoltage = -50.0;

    // Number of cells
    const unsigned int numNeurons = (int) std::sqrt(NSYN / 0.02);//10000;

    const unsigned int numTimesteps = 10000;//10000;

    // connection probability
    const double probabilityConnection = 0.02;

    // number of excitatory cells:number of inhibitory cells
    const double excitatoryInhibitoryRatio = 4.0;

	// Rate of Poisson noise injected into each neuron (Hz)
    const double inputRate = 20.0;

    const unsigned int numExcitatory = (unsigned int)std::round(((double)numNeurons * excitatoryInhibitoryRatio) / (1.0 + excitatoryInhibitoryRatio));
    const unsigned int numInhibitory = numNeurons - numExcitatory;

    const double scale = 16000000.0 / numNeurons / numNeurons;

    const double excitatoryWeight = 0.4e-8f * scale;
    const double inhibitoryWeight = 5.1e-8f * scale;

    // Axonal delay
    const double delayMs = 0.8;

    const unsigned int delayTimesteps = (unsigned int)std::round(delayMs / timestep);

}