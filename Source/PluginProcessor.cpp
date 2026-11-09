#include "PluginProcessor.h"
#include "UI/PluginEditor.h"

using namespace juce;

SIXAudioProcessor::SIXAudioProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput ("Input", AudioChannelSet::stereo(), true)
                        .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SIXPARAMS", six::createParameterLayout())
{
    params.bind (apvts);
    host = std::make_unique<six::HostManager> (*this, engine, params);
}

SIXAudioProcessor::~SIXAudioProcessor()
{
    host.reset();
}

bool SIXAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    const auto in = l.getMainInputChannelSet();
    if (out != AudioChannelSet::stereo() && out != AudioChannelSet::mono()) return false;
    return in == AudioChannelSet::stereo() || in == AudioChannelSet::mono() || in.isDisabled();
}

void SIXAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    engine.prepare (sampleRate, samplesPerBlock);
    host->prepareAll (sampleRate, jmax (16, samplesPerBlock), isNonRealtime());
    setLatencySamples (engine.neededLatency.load());
}

void SIXAudioProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    midi.clear();

    six::BlockContext ctx;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) lastBpm = *bpm;
            if (auto ts = pos->getTimeSignature()) { lastNum = ts->numerator; lastDen = ts->denominator; }
        }
    ctx.bpm = lastBpm;
    ctx.timeSigNum = lastNum;
    ctx.timeSigDen = lastDen;
    ctx.offline = isNonRealtime();

    engine.process (buffer, getTotalNumInputChannels(), ctx);
}

AudioProcessorEditor* SIXAudioProcessor::createEditor()
{
    return new SIXEditor (*this);
}

void SIXAudioProcessor::getStateInformation (MemoryBlock& dest)
{
    XmlElement root ("SIX");
    root.setAttribute ("version", 1);
    root.setAttribute ("selectedChannel", selectedChannel);
    root.setAttribute ("presetPage", presetPage);
    if (auto p = apvts.copyState().createXml()) root.addChildElement (p.release());

    // Hosts call this on the message thread; reading the model from another thread is still safe enough
    // (no allocation races with the audio thread, which never touches the model).
    auto h = host->saveState();
    if (h != nullptr) root.addChildElement (h.release());
    copyXmlToBinary (root, dest);
}

void SIXAudioProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName ("SIX")) return;

    if (MessageManager::getInstance()->isThisTheMessageThread())
        applyState (*xml);
    else
    {
        std::shared_ptr<XmlElement> copy (xml.release());
        MessageManager::callAsync ([this, copy] { applyState (*copy); });
    }
}

void SIXAudioProcessor::applyState (const XmlElement& xml)
{
    selectedChannel = jlimit (0, six::kNumChannels - 1, xml.getIntAttribute ("selectedChannel", 0));
    presetPage = jlimit (0, six::kNumPresets / six::kPresetsPerPage - 1, xml.getIntAttribute ("presetPage", 0));
    if (auto* p = xml.getChildByName (apvts.state.getType()))
        apvts.replaceState (ValueTree::fromXml (*p));
    if (auto* h = xml.getChildByName ("HOST"))
        host->restoreState (*h);
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new SIXAudioProcessor();
}
