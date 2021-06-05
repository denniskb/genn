// Standard C++ includes
#include <iostream>
#include <random>

// GeNN robotics includes
#include "timer.h"
#include "spikeRecorder.h"

// Model parameters
#include "parameters.h"

// Auto-generated model code
#include "brunel_CODE/definitions.h"

int main()
{
    try
    {
		printf("{\n");
		printf("\t\"sim\": \"genn\",\n");
		#ifdef STDP
		printf("\t\"model\": \"brunel+\",\n");
		#else
		printf("\t\"model\": \"brunel\",\n");
		#endif
		printf("\t\"#syn\": %.2e,\n", 0.2 * Parameters::numNeurons * Parameters::numNeurons);
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

		writeTextSpikeRecording("spikes_e.csv", recordSpkE, Parameters::numExcitatory, 
								Parameters::numTimesteps, Parameters::timestep);
		writeTextSpikeRecording("spikes_i.csv", recordSpkI, Parameters::numInhibitory, 
								Parameters::numTimesteps, Parameters::timestep);*/
    }
    catch(const std::exception &ex) {
        std::cerr << "Error:" << ex.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}