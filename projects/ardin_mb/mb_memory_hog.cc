#include "mb_memory_hog.h"

// Standard C++ includes
#include <bitset>
#include <fstream>
#include <random>

// BoB robotics includes
#include "common/timer.h"
#include "genn_utils/connectors.h"

// GeNN generated code includes
#include "ardin_mb_CODE/definitions.h"

// Antworld includes
#include "mb_params.h"

using namespace BoBRobotics;

//----------------------------------------------------------------------------
// Anonymous namespace
//----------------------------------------------------------------------------
namespace
{
unsigned int convertMsToTimesteps(double ms)
{
    return (unsigned int)std::round(ms / MBParams::timestepMs);
}
void record(double t, unsigned int spikeCount, unsigned int *spikes, MBMemoryHOG::Spikes &spikeOutput)
{
    // Add a new entry to the cache
    spikeOutput.emplace_back();

    // Fill in time
    spikeOutput.back().first = t;

    // Reserve vector to hold spikes
    spikeOutput.back().second.reserve(spikeCount);

    // Copy spikes into vector
    std::copy_n(spikes, spikeCount, std::back_inserter(spikeOutput.back().second));
}
}   // Anonymous namespace

//----------------------------------------------------------------------------
// MBMemory
//----------------------------------------------------------------------------
MBMemoryHOG::MBMemoryHOG()
    :   Navigation::VisualNavigationBase(cv::Size(MBParams::inputWidth, MBParams::inputHeight)),
        m_HOGFeatures(MBParams::hogFeatureSize), m_NumPNSpikes(0), m_NumKCSpikes(0),
        m_NumUsedWeights(MBParams::numKC * MBParams::numEN), m_NumActivePN(0), m_NumActiveKC(0),
        m_RateScalePN(MBParams::inputRateScale), m_PNToKCTauSyn(3.0f), m_RewardTimeMs(MBParams::rewardTimeMs), m_PresentDurationMs(MBParams::presentDurationMs)
{
    std::cout << "HOG feature vector length:" << MBParams::hogFeatureSize << std::endl;

    // Configure HOG features
    m_HOG.winSize = cv::Size(MBParams::inputWidth, MBParams::inputHeight);
    m_HOG.blockSize = m_HOG.winSize;
    m_HOG.blockStride = m_HOG.winSize;
    m_HOG.cellSize = cv::Size(MBParams::hogCellSize, MBParams::hogCellSize);
    m_HOG.nbins = MBParams::hogNumOrientations;

    std::mt19937 gen;

    {
        Timer<> timer("Allocation:");
        allocateMem();
    }

    {
        Timer<> timer("Initialization:");
        initialize();
    }

    {
        Timer<> timer("Building connectivity:");

        GeNNUtils::buildFixedNumberPreConnector(MBParams::numPN, MBParams::numKC,
                                                MBParams::numPNSynapsesPerKC, CpnToKC, &allocatepnToKC, gen);
    }

    // Final setup
    {
        Timer<> timer("Sparse init:");
        initardin_mb();
    }
#ifndef CPU_ONLY
    CHECK_CUDA_ERRORS(cudaMalloc(&m_HOGFeaturesGPU, MBParams::hogFeatureSize * sizeof(float)));
#endif

    // Set initial weights
    *getGGNToKCWeight() = MBParams::ggnToKCWeight;
    *getKCToGGNWeight() = MBParams::kcToGGNWeight;
    *getPNToKC() = MBParams::pnToKCWeight;
}
//----------------------------------------------------------------------------
MBMemoryHOG::~MBMemoryHOG()
{
#ifndef CPU_ONLY
    CHECK_CUDA_ERRORS(cudaFree(m_HOGFeaturesGPU));
#endif
}
//----------------------------------------------------------------------------
void MBMemoryHOG::train(const cv::Mat &image)
{
    present(image, true);
}
//----------------------------------------------------------------------------
float MBMemoryHOG::test(const cv::Mat &image) const
{
    // Get number of EN spikes
    unsigned int numENSpikes = std::get<2>(present(image, false));
    //std::cout << "\t" << numENSpikes << " EN spikes" << std::endl;

    // Largest difference would be expressed by EN firing every timestep
    return (float)numENSpikes / (float)(convertMsToTimesteps(MBParams::presentDurationMs) + convertMsToTimesteps(MBParams::postStimuliDurationMs));
}
//----------------------------------------------------------------------------
void MBMemoryHOG::clearMemory()
{
    throw std::runtime_error("MBMemory does not currently support clearing");
}
//----------------------------------------------------------------------------
float *MBMemoryHOG::getGGNToKCWeight()
{
    return &gggnToKC;
}
//----------------------------------------------------------------------------
float *MBMemoryHOG::getKCToGGNWeight()
{
    return &gkcToGGN;
}
//----------------------------------------------------------------------------
float *MBMemoryHOG::getPNToKC()
{
    return &gpnToKC;
}
//----------------------------------------------------------------------------
void MBMemoryHOG::write(cv::FileStorage& fs) const
{
    fs << "config" << "{";
    fs << "rewardTimeMs" << m_RewardTimeMs;
    fs << "presentDurationMs" << m_PresentDurationMs;

    fs << "ggnToKC" << "{";
    fs << "weight" << gggnToKC;
    fs << "}";

    fs << "kcToGGC" << "{";
    fs << "weight" << gkcToGGN;
    fs << "}";

    fs << "pnToKC" << "{";
    fs << "weight" << gpnToKC;
    fs << "tauSyn" << m_PNToKCTauSyn;
    fs << "}";
    fs << "}";
}
//----------------------------------------------------------------------------
void MBMemoryHOG::read(const cv::FileNode &node)
{
    cv::read(node["rewardTimeMs"], m_RewardTimeMs, m_RewardTimeMs);
    cv::read(node["presentDurationMs"], m_PresentDurationMs, m_PresentDurationMs);

    const auto &ggnToKC = node["ggnToKC"];
    if(ggnToKC.isMap()) {
        ggnToKC["weight"] >> gggnToKC;
    }

    const auto &kcToGGC = node["kcToGGC"];
    if(kcToGGC.isMap()) {
        kcToGGC["weight"] >> gkcToGGN;
    }

    const auto &pnToKC = node["pnToKC"];
    if(pnToKC.isMap()) {
        pnToKC["weight"] >> gpnToKC;
        pnToKC["tauSyn"] >> m_PNToKCTauSyn;
    }

}
//----------------------------------------------------------------------------
std::tuple<unsigned int, unsigned int, unsigned int> MBMemoryHOG::present(const cv::Mat &image, bool train) const
{
    BOB_ASSERT(image.cols == MBParams::inputWidth);
    BOB_ASSERT(image.rows == MBParams::inputHeight);
    BOB_ASSERT(image.type() == CV_8UC1);

    // Compute HOG features
    m_HOG.compute(image, m_HOGFeatures);
    BOB_ASSERT(m_HOGFeatures.size() == MBParams::hogFeatureSize);


    expDecaypnToKC = (float)std::exp(-MBParams::timestepMs / m_PNToKCTauSyn);
    initpnToKC = (float)(m_PNToKCTauSyn * (1.0 - std::exp(-MBParams::timestepMs / m_PNToKCTauSyn))) * (1.0 / MBParams::timestepMs);
    //const float magnitude = std::accumulate(m_HOGFeatures.begin(), m_HOGFeatures.end(), 0.0f);
    /*std::transform(m_HOGFeatures.begin(), m_HOGFeatures.end(), m_HOGFeatures.begin(),
                   [magnitude](float f)
                   {
                       return f / magnitude;
                   });*/
    //std::cout << "HOG feature magnitude:" << magnitude << std::endl;

    // Convert HOG features into lamnda
    /*std::transform(m_HOGFeatures.begin(), m_HOGFeatures.end(), expMinusLambdaPN,
                   [&](float f)
                   {
                       return std::exp(-((f * m_RateScalePN) / 1000.0) * MBParams::timestepMs);
                   });*/

    // Copy HOG features into GeNN variable
    //std::copy(m_HOGFeatures.begin(), m_HOGFeatures.end(), ratePN);
    /*std::exponential_distribution<float> standardExponentialDistribution(1.0f);
    std::transform(m_HOGFeatures.begin(), m_HOGFeatures.end(), timeToSpikePN,
                   [this, &standardExponentialDistribution](float f)
                   {
                       return (1000.0f / (m_RateScalePN * f)) * standardExponentialDistribution(m_RNG);
                   });*/
    const float maxHOG = *std::max_element(m_HOGFeatures.begin(), m_HOGFeatures.end());
    std::transform(m_HOGFeatures.begin(), m_HOGFeatures.end(), timeToSpikePN,
                   [this, maxHOG](float f)
                   {
                       return (maxHOG - f) * (m_PresentDurationMs / maxHOG);
                   });

    // Make sure KC state is reset before simulation
    std::fill_n(VKC, MBParams::numKC, -60.0f);


#ifndef CPU_ONLY
    pushPNStateToDevice();
    pushKCStateToDevice();
#endif

    // Reset model time
    iT = 0;
    t = 0.0f;

    // Convert simulation regime parameters to timesteps
    const unsigned long long rewardTimestep = convertMsToTimesteps(m_RewardTimeMs);
    const unsigned int endPresentTimestep = convertMsToTimesteps(m_PresentDurationMs);
    const unsigned int postStimuliDuration = convertMsToTimesteps(MBParams::postStimuliDurationMs);

    const long long duration = endPresentTimestep + postStimuliDuration;

    // Clear GGN voltage and reserve
    m_GGNVoltage.clear();
    m_GGNVoltage.reserve(duration);

    // Open CSV output files
    const float startTimeMs = t;

    // Clear spike records
    m_PNSpikes.clear();
    m_KCSpikes.clear();
    m_ENSpikes.clear();

    std::bitset<MBParams::numPN> pnSpikeBitset;
    std::bitset<MBParams::numKC> kcSpikeBitset;

    // Loop through timesteps
    m_NumPNSpikes = 0;
    m_NumKCSpikes = 0;
    unsigned int numENSpikes = 0;
    while(iT < duration) {
        // If we should stop presenting image
        if(iT == endPresentTimestep) {
            std::fill_n(timeToSpikePN, MBParams::numPN, MBParams::postStimuliDurationMs + m_PresentDurationMs + 10.0f);

#ifndef CPU_ONLY
            pushPNStateToDevice();
#endif
        }

        // If we should reward in this timestep, inject dopamine
        if(train && iT == rewardTimestep) {
            injectDopaminekcToEN = true;
        }

#ifndef CPU_ONLY
        // Simulate on GPU
        stepTimeGPU();

        // Download spikes
        pullPNCurrentSpikesFromDevice();
        pullKCCurrentSpikesFromDevice();
        pullENCurrentSpikesFromDevice();
        pullGGNStateFromDevice();
#else
        // Simulate on CPU
        stepTimeCPU();
#endif
        // If a dopamine spike has been injected this timestep
        if(injectDopaminekcToEN) {
            // Decay global dopamine traces
            dkcToEN = dkcToEN * std::exp(-(t - tDkcToEN) / MBParams::tauD);

            // Add effect of dopamine spike
            dkcToEN += MBParams::dopamineStrength;

            // Update last reward time
            tDkcToEN = t;

            // Clear dopamine injection flags
            injectDopaminekcToEN = false;
        }

        numENSpikes += spikeCount_EN;
        m_NumPNSpikes += spikeCount_PN;
        m_NumKCSpikes += spikeCount_KC;
        for(unsigned int i = 0; i < spikeCount_PN; i++) {
            pnSpikeBitset.set(spike_PN[i]);
        }

        for(unsigned int i = 0; i < spikeCount_KC; i++) {
            kcSpikeBitset.set(spike_KC[i]);
        }

        // Record spikes
        record(t, spikeCount_PN, spike_PN, m_PNSpikes);
        record(t, spikeCount_KC, spike_KC, m_KCSpikes);
        record(t, spikeCount_EN, spike_EN, m_ENSpikes);

        // Record GGN voltage
        m_GGNVoltage.push_back(VGGN[0]);
    }

    std::cout << std::endl;
#ifdef RECORD_TERMINAL_SYNAPSE_STATE
    // Download synaptic state
    pullkcToENStateFromDevice();

    std::ofstream terminalStream("terminal_synaptic_state.csv");
    terminalStream << "Weight, Eligibility" << std::endl;
    for(unsigned int s = 0; s < MBParams::numKC * MBParams::numEN; s++) {
        terminalStream << gkcToEN[s] << "," << ckcToEN[s] * std::exp(-(t - tCkcToEN[s]) / 40.0) << std::endl;
    }
    std::cout << "Final dopamine level:" << dkcToEN * std::exp(-(t - tDkcToEN) / MBParams::tauD) << std::endl;
#endif  // RECORD_TERMINAL_SYNAPSE_STATE

    // Cache number of unique active cells
    m_NumActivePN = pnSpikeBitset.count();
    m_NumActiveKC = kcSpikeBitset.count();

    if(train) {
        constexpr unsigned int numWeights = MBParams::numKC * MBParams::numEN;

#ifndef CPU_ONLY
        CHECK_CUDA_ERRORS(cudaMemcpy(gkcToEN, d_gkcToEN, numWeights * sizeof(scalar), cudaMemcpyDeviceToHost));
#endif  // CPU_ONLY

        // Cache number of unused weights
        m_NumUsedWeights = numWeights - std::count(&gkcToEN[0], &gkcToEN[numWeights], 0.0f);
    }

    return std::make_tuple(m_NumPNSpikes, m_NumKCSpikes, numENSpikes);
}
