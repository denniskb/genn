// Standard C++ includes
#include <iostream>
#include <random>

// GeNN robotics includes
#include "timer.h"
#include "spikeRecorder.h"

// Model parameters
#include "parameters.h"

// Auto-generated model code
#include "synth_CODE/definitions.h"

int main()
{
    try
    {
		printf("{\n");
		printf("\t\"sim\": \"genn\",\n");
		printf("\t\"model\": \"synth_0.00156_0.005_1\",\n");
		printf("\t\"#syn\": %.2e,\n", 0.00156 * Parameters::numNeurons * Parameters::numNeurons);
		printf("\t\"#gpus\": 1,\n");
		{
			Timer t("\t\"setuptime\": ");
			allocateMem();
			//allocateRecordingBuffers(Parameters::numTimesteps);
			initialize();
			initializeSparse();
		}
		printf(",\n");

        {
            Timer t("\t\"simtime\": ");
            while(iT < Parameters::numTimesteps) {
                stepTime();
            }
		}
		printf("\n");
		printf("}\n");

		/*pullRecordingBuffersFromDevice();

		writeTextSpikeRecording("spikes_e.csv", recordSpkP, Parameters::numNeurons, 
								Parameters::numTimesteps, Parameters::timestep);*/
    }
    catch(const std::exception &ex) {
        std::cerr << "Error:" << ex.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}