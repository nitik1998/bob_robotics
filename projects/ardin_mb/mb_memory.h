#pragma once


// Standard C++ includes
#include <array>
#include <bitset>
#include <list>
#include <random>
#include <tuple>
#include <vector>

// OpenCV includes
#include <opencv2/opencv.hpp>

// BoB robotics includes
#include "genn_utils/shared_library_model.h"
#include "navigation/visual_navigation_base.h"

// Ardin MB includes
#include "mb_params_hog.h"

// Forward declarations
namespace CLI
{
class App;
}

//----------------------------------------------------------------------------
// MBMemory
//----------------------------------------------------------------------------
class MBMemory : public BoBRobotics::Navigation::VisualNavigationBase
{
public:
    MBMemory(unsigned int numPN, unsigned int numKC, unsigned int numEN, unsigned int numPNSynapsesPerKC,
             int inputWidth, int inputHeight,
             double tauD, double kcToENWeight, double dopamineStrength,
             double rewardTimeMs, double presentDurationMs, double timestepMs,
             const std::string &modelName);
    virtual ~MBMemory();

    //------------------------------------------------------------------------
    // Typedefines
    //------------------------------------------------------------------------
    typedef std::list<std::pair<double, std::vector<unsigned int>>> Spikes;

    //------------------------------------------------------------------------
    // VisualNavigationBase virtuals
    //------------------------------------------------------------------------
    //! Train the algorithm with the specified image
    virtual void train(const cv::Mat &image) override;

    //! Test the algorithm with the specified image
    virtual float test(const cv::Mat &image) const override;

    //! Clear the memory
    virtual void clearMemory() override;

    //------------------------------------------------------------------------
    // Declared virtuals
    //------------------------------------------------------------------------
    virtual void write(cv::FileStorage &fs) const;
    virtual void read(const cv::FileNode &node);

    virtual void addCLIArguments(CLI::App&){}

    //------------------------------------------------------------------------
    // Public API
    //------------------------------------------------------------------------
    float *getRewardTimeMs(){ return &m_RewardTimeMs; }
    float *getPresentDurationMs(){ return &m_PresentDurationMs; }
    float *getKCToENDopamineStrength(){ return &m_KCToENDopamineStrength; }

    const Spikes &getPNSpikes() const{ return m_PNSpikes; }
    const Spikes &getKCSpikes() const{ return m_KCSpikes; }
    const Spikes &getENSpikes() const{ return m_ENSpikes; }

    unsigned int getNumPNSpikes() const{ return m_NumPNSpikes; }
    unsigned int getNumKCSpikes() const{ return m_NumKCSpikes; }
    unsigned int getNumENSpikes() const{ return m_NumENSpikes; }

    unsigned int getNumUnusedWeights() const{ return m_NumUsedWeights; }
    unsigned int getNumActivePN() const{ return m_NumActivePN; }
    unsigned int getNumActiveKC() const{ return m_NumActiveKC; }

protected:
    //------------------------------------------------------------------------
    // Declared virtuals
    //------------------------------------------------------------------------
    virtual void initPresent(unsigned long long duration) const = 0;
    virtual void beginPresent(const cv::Mat &snapshotFloat) const = 0;
    virtual void endPresent() const = 0;
    virtual void recordAdditional() const{}

    //------------------------------------------------------------------------
    // Protected methods
    //------------------------------------------------------------------------
    unsigned int convertMsToTimesteps(double ms) const
    {
        return (unsigned int)std::round(ms / m_TimestepMs);
    }

    BoBRobotics::GeNNUtils::SharedLibraryModelFloat &getSLM() const
    {
        return m_SLM;
    }

private:
    //------------------------------------------------------------------------
    // Private API
    //------------------------------------------------------------------------
    std::tuple<unsigned int, unsigned int, unsigned int> present(const cv::Mat &image, bool train) const;

    //------------------------------------------------------------------------
    // Members
    //------------------------------------------------------------------------
    // Floating point version of snapshot
    mutable cv::Mat m_SnapshotFloat;

    // Model parameters
    const unsigned int m_NumPN;
    const unsigned int m_NumKC;
    const unsigned int m_NumEN;
    const unsigned int m_NumPNSynapsesPerKC;

    const int m_InputWidth;
    const int m_InputHeight;
    const double m_TauD;
    const double m_KCToENWeight;
    const double m_TimestepMs;

    // Model extra global parameters used to provide dopamine signal
    float *m_TDKCToEN;
    float *m_DKCToEN;
    bool *m_InjectDopamineKCToEN;

    // Model state variables
    unsigned int *m_SpkCntEN;
    unsigned int *m_SpkEN;
    unsigned int *m_SpkCntKC;
    unsigned int *m_SpkKC;
    unsigned int *m_SpkCntPN;
    unsigned int *m_SpkPN;
    float *m_GKCToEN;

    // Simulation parameters
    float m_KCToENDopamineStrength;
    float m_RewardTimeMs;
    float m_PresentDurationMs;

    // Spike recording infrastructure
    mutable Spikes m_PNSpikes;
    mutable Spikes m_KCSpikes;
    mutable Spikes m_ENSpikes;

    mutable unsigned int m_NumPNSpikes;
    mutable unsigned int m_NumKCSpikes;
    mutable unsigned int m_NumENSpikes;

    mutable unsigned int m_NumUsedWeights;
    mutable unsigned int m_NumActivePN;
    mutable unsigned int m_NumActiveKC;

    mutable BoBRobotics::GeNNUtils::SharedLibraryModelFloat m_SLM;
};
