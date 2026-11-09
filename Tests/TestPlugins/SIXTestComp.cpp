// Deterministic test compressor (instant peak detection) with a Threshold parameter.
#include <juce_audio_utils/juce_audio_utils.h>
using namespace juce;

class TestComp : public AudioProcessor
{
public:
    TestComp() : AudioProcessor (BusesProperties().withInput ("In", AudioChannelSet::stereo()).withOutput ("Out", AudioChannelSet::stereo()))
    {
        addParameter (threshold = new AudioParameterFloat (ParameterID { "threshold", 1 }, "Threshold", -60.0f, 0.0f, 0.0f));
        addParameter (ratio = new AudioParameterFloat (ParameterID { "ratio", 1 }, "Ratio", 1.0f, 20.0f, 4.0f));
    }
    const String getName() const override { return "SIXTestComp"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    { return l.getMainInputChannelSet() == l.getMainOutputChannelSet() && ! l.getMainInputChannelSet().isDisabled() && l.getMainInputChannelSet().size() <= 2; }
    void processBlock (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const float thr = threshold->get(), r = ratio->get();
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            float pk = 0;
            for (int c = 0; c < getTotalNumInputChannels(); ++c) pk = jmax (pk, std::abs (b.getSample (c, i)));
            const float db = Decibels::gainToDecibels (pk, -120.0f);
            const float g = db > thr ? Decibels::decibelsToGain ((thr + (db - thr) / r) - db) : 1.0f;
            for (int c = 0; c < getTotalNumInputChannels(); ++c) b.setSample (c, i, b.getSample (c, i) * g);
        }
    }
    AudioProcessorEditor* createEditor() override { return new GenericAudioProcessorEditor (*this); }
    bool hasEditor() const override { return true; }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const String getProgramName (int) override { return {}; }
    void changeProgramName (int, const String&) override {}
    void getStateInformation (MemoryBlock& m) override
    { MemoryOutputStream s (m, false); s.writeFloat (*threshold); s.writeFloat (*ratio); }
    void setStateInformation (const void* d, int n) override
    { MemoryInputStream s (d, (size_t) n, false); *threshold = s.readFloat(); *ratio = s.readFloat(); }
    AudioParameterFloat* threshold; AudioParameterFloat* ratio;
};
AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestComp(); }
