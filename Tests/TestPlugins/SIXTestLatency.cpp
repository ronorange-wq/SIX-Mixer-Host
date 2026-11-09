// Delays audio by 100 samples and reports that latency.
#include <juce_audio_utils/juce_audio_utils.h>
using namespace juce;
class TestLatency : public AudioProcessor
{
public:
    TestLatency() : AudioProcessor (BusesProperties().withInput ("In", AudioChannelSet::stereo()).withOutput ("Out", AudioChannelSet::stereo()))
    { addParameter (gain = new AudioParameterFloat (ParameterID { "gain", 1 }, "Gain", 0.0f, 1.0f, 1.0f)); }
    static constexpr int L = 100;
    const String getName() const override { return "SIXTestLatency"; }
    void prepareToPlay (double, int) override { setLatencySamples (L); hist.setSize (2, L); hist.clear(); pos = 0; }
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    { return l.getMainInputChannelSet() == AudioChannelSet::stereo() && l.getMainOutputChannelSet() == AudioChannelSet::stereo(); }
    void processBlock (AudioBuffer<float>& b, MidiBuffer&) override
    {
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                const float x = b.getSample (c, i);
                b.setSample (c, i, hist.getSample (c, pos) * gain->get());
                hist.setSample (c, pos, x);
            }
            pos = (pos + 1) % L;
        }
    }
    AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const String getProgramName (int) override { return {}; }
    void changeProgramName (int, const String&) override {}
    void getStateInformation (MemoryBlock& m) override { MemoryOutputStream s (m, false); s.writeFloat (*gain); }
    void setStateInformation (const void* d, int n) override { MemoryInputStream s (d, (size_t) n, false); *gain = s.readFloat(); }
    AudioParameterFloat* gain; AudioBuffer<float> hist; int pos = 0;
};
AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestLatency(); }
