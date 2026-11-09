// Mono in -> stereo out (like a mono-in reverb): L = x, R = -x * width.
#include <juce_audio_utils/juce_audio_utils.h>
using namespace juce;
class TestWidener : public AudioProcessor
{
public:
    TestWidener() : AudioProcessor (BusesProperties().withInput ("In", AudioChannelSet::mono()).withOutput ("Out", AudioChannelSet::stereo()))
    { addParameter (width = new AudioParameterFloat (ParameterID { "width", 1 }, "Width", 0.0f, 1.0f, 1.0f)); }
    const String getName() const override { return "SIXTestWidener"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    { return l.getMainInputChannelSet() == AudioChannelSet::mono() && l.getMainOutputChannelSet() == AudioChannelSet::stereo(); }
    void processBlock (AudioBuffer<float>& b, MidiBuffer&) override
    {
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float x = b.getSample (0, i);
            b.setSample (0, i, x);
            b.setSample (1, i, -x * width->get());
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
    void getStateInformation (MemoryBlock& m) override { MemoryOutputStream s (m, false); s.writeFloat (*width); }
    void setStateInformation (const void* d, int n) override { MemoryInputStream s (d, (size_t) n, false); *width = s.readFloat(); }
    AudioParameterFloat* width;
};
AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestWidener(); }
