#include "PluginProcessor.h"
#include <algorithm>
#include <cmath>

using namespace juce;

//==============================================================================
ChipSynthAudioProcessor::ChipSynthAudioProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
    held.reserve (64);
    arpNotes.reserve (64);
}

AudioProcessorValueTreeState::ParameterLayout ChipSynthAudioProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout layout;

    auto choice = [&] (const char* id, const char* name, StringArray items, int def)
    {
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def));
    };
    auto flt = [&] (const char* id, const char* name, float lo, float hi, float skew, float def)
    {
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name,
                                                           NormalisableRange<float> (lo, hi, 0.f, skew), def));
    };

    choice ("wave", "Waveform",    { "Pulse", "Triangle", "Noise", "Metal Noise" }, 0);
    choice ("duty", "Pulse Width", { "12.5%", "25%", "50%", "75%" }, 1);

    flt ("attack",  "Attack (ms)",  0.f, 2000.f, 0.3f, 0.f);
    flt ("decay",   "Decay (ms)",   0.f, 3000.f, 0.3f, 300.f);
    flt ("sustain", "Sustain",      0.f, 1.f,    1.0f, 0.6f);
    flt ("release", "Release (ms)", 0.f, 3000.f, 0.3f, 120.f);

    flt ("vibrate",  "Vibrato Rate (Hz)",     0.1f, 20.f, 0.5f, 6.f);
    flt ("vibdepth", "Vibrato Depth (semis)", 0.f,  2.f,  0.5f, 0.f);
    flt ("sweep",    "Pitch Sweep (semis/s)", -48.f, 48.f, 1.0f, 0.f);

    choice ("arpmode", "Arpeggio", { "Off", "Held Notes", "Major", "Minor", "Octaves" }, 0);
    flt ("arprate", "Arp Speed (Hz)", 1.f, 60.f, 0.5f, 15.f);

    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "bits", 1 }, "Bit Depth", 1, 16, 16));
    flt ("downsample", "Downsample", 1.f, 32.f, 0.5f, 1.f);

    flt ("volume", "Volume", 0.f, 1.f, 1.0f, 0.5f);

    return layout;
}

//==============================================================================
void ChipSynthAudioProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    held.clear();
    arpNotes.clear();
    stage = Stage::Idle;
    env = 0.f;
    crushHeld = 0.f;
    lfsr = 1;
}

bool ChipSynthAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == AudioChannelSet::mono() || out == AudioChannelSet::stereo();
}

void ChipSynthAudioProcessor::readParams()
{
    auto get = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };

    p.wave       = (int) get ("wave");
    p.duty       = jlimit (0, 3, (int) get ("duty"));
    p.attackMs   = get ("attack");
    p.decayMs    = get ("decay");
    p.sustain    = get ("sustain");
    p.releaseMs  = get ("release");
    p.vibRate    = get ("vibrate");
    p.vibDepth   = get ("vibdepth");
    p.sweep      = get ("sweep");
    p.arpMode    = (int) get ("arpmode");
    p.arpRate    = get ("arprate");
    p.bits       = get ("bits");
    p.downsample = get ("downsample");
    p.volume     = get ("volume");
}

//==============================================================================
void ChipSynthAudioProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    readParams();
    buffer.clear();

    int pos = 0;
    for (const auto metadata : midi)
    {
        const int at = jlimit (pos, buffer.getNumSamples(), metadata.samplePosition);
        render (buffer, pos, at);
        handleMidi (metadata.getMessage());
        pos = at;
    }
    render (buffer, pos, buffer.getNumSamples());
}

void ChipSynthAudioProcessor::render (AudioBuffer<float>& buffer, int start, int end)
{
    auto* left  = buffer.getWritePointer (0);
    auto* right = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int i = start; i < end; ++i)
    {
        const float s = nextSample();
        left[i] = s;
        if (right != nullptr)
            right[i] = s;
    }
}

//==============================================================================
void ChipSynthAudioProcessor::handleMidi (const MidiMessage& m)
{
    if (m.isNoteOn())
        noteOn (m.getNoteNumber(), m.getFloatVelocity());
    else if (m.isNoteOff())
        noteOff (m.getNoteNumber());
    else if (m.isAllNotesOff() || m.isAllSoundOff())
    {
        held.clear();
        arpNotes.clear();
        beginRelease();
    }
}

void ChipSynthAudioProcessor::noteOn (int note, float vel)
{
    held.erase (std::remove (held.begin(), held.end(), note), held.end());
    held.push_back (note);
    rebuildArpNotes();

    currentNote = note;
    velocity    = vel;
    sweepSemis  = 0.f;
    arpStep     = 0;
    arpCounter  = 0.0;
    vibPhase    = 0.0;
    if (p.wave < 2)
        phase = 0.0;

    stage = Stage::Attack;
}

void ChipSynthAudioProcessor::noteOff (int note)
{
    held.erase (std::remove (held.begin(), held.end(), note), held.end());
    rebuildArpNotes();

    if (held.empty())
        beginRelease();
    else
        currentNote = held.back();
}

void ChipSynthAudioProcessor::beginRelease()
{
    if (stage == Stage::Idle)
        return;

    const double relSamples = std::max (1.0, (double) p.releaseMs * 0.001 * sr);
    releaseStep = (float) (std::max (env, 0.0001f) / relSamples);
    stage = Stage::Release;
}

void ChipSynthAudioProcessor::rebuildArpNotes()
{
    arpNotes = held;
    std::sort (arpNotes.begin(), arpNotes.end());
}

//==============================================================================
void ChipSynthAudioProcessor::stepEnvelope()
{
    switch (stage)
    {
        case Stage::Idle:
            break;

        case Stage::Attack:
            env += (float) (1.0 / std::max (1.0, (double) p.attackMs * 0.001 * sr));
            if (env >= 1.f) { env = 1.f; stage = Stage::Decay; }
            break;

        case Stage::Decay:
            env -= (float) ((1.0 - p.sustain) / std::max (1.0, (double) p.decayMs * 0.001 * sr));
            if (env <= p.sustain) { env = p.sustain; stage = Stage::Sustain; }
            break;

        case Stage::Sustain:
            env = p.sustain;
            break;

        case Stage::Release:
            env -= releaseStep;
            if (env <= 0.f) { env = 0.f; stage = Stage::Idle; }
            break;
    }
}

float ChipSynthAudioProcessor::nextSample()
{
    stepEnvelope();

    if (stage == Stage::Idle)
    {
        crushHeld = 0.f;
        return 0.f;
    }

    // ---- arpeggiator -------------------------------------------------------
    int note = currentNote;
    int arpOffset = 0;

    if (p.arpMode != 0)
    {
        arpCounter += p.arpRate / sr;
        while (arpCounter >= 1.0) { arpCounter -= 1.0; ++arpStep; }

        static const int majorOffsets[] = { 0, 4, 7 };
        static const int minorOffsets[] = { 0, 3, 7 };
        static const int octOffsets[]   = { 0, 12 };

        switch (p.arpMode)
        {
            case 1: if (! arpNotes.empty()) note = arpNotes[(size_t) arpStep % arpNotes.size()]; break;
            case 2: arpOffset = majorOffsets[arpStep % 3]; break;
            case 3: arpOffset = minorOffsets[arpStep % 3]; break;
            case 4: arpOffset = octOffsets[arpStep % 2];   break;
            default: break;
        }
    }

    // ---- pitch: vibrato + sweep -------------------------------------------
    vibPhase += p.vibRate / sr;
    if (vibPhase >= 1.0) vibPhase -= 1.0;

    sweepSemis = jlimit (-72.f, 72.f, sweepSemis + (float) (p.sweep / sr));

    const double semis = (double) note + arpOffset + sweepSemis
                       + p.vibDepth * std::sin (MathConstants<double>::twoPi * vibPhase);
    const double freq  = jmin (440.0 * std::pow (2.0, (semis - 69.0) / 12.0), sr * 0.45);

    // ---- oscillator --------------------------------------------------------
    float osc = 0.f;

    phase += freq / sr;
    if (phase >= 1.0) phase -= 1.0;

    if (p.wave == 0)         // pulse
    {
        static const double duties[] = { 0.125, 0.25, 0.5, 0.75 };
        osc = phase < duties[p.duty] ? 1.f : -1.f;
    }
    else if (p.wave == 1)    // 4-bit stepped triangle (NES style)
    {
        const float tri = phase < 0.5 ? (float) (4.0 * phase - 1.0) : (float) (3.0 - 4.0 * phase);
        osc = std::floor ((tri * 0.5f + 0.5f) * 15.999f) / 7.5f - 1.f;
    }
    else                     // 15-bit LFSR noise, "Metal" uses the short 93-step mode
    {
        noiseAcc += freq * 16.0 / sr;
        while (noiseAcc >= 1.0)
        {
            noiseAcc -= 1.0;
            const uint16_t fb = (uint16_t) ((lfsr ^ (lfsr >> (p.wave == 3 ? 6 : 1))) & 1);
            lfsr = (uint16_t) ((lfsr >> 1) | (fb << 14));
            if (lfsr == 0) lfsr = 1;
        }
        osc = (lfsr & 1) ? -1.f : 1.f;
    }

    // ---- amplitude + bit crusher ------------------------------------------
    const float out = osc * env * velocity;

    crushCounter += 1.0;
    if (crushCounter >= (double) p.downsample)
    {
        crushCounter -= (double) p.downsample;
        const float levels = std::pow (2.f, p.bits - 1.f);
        crushHeld = std::round (out * levels) / levels;
    }

    return crushHeld * p.volume;
}

//==============================================================================
AudioProcessorEditor* ChipSynthAudioProcessor::createEditor()
{
    return new GenericAudioProcessorEditor (*this);
}

void ChipSynthAudioProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void ChipSynthAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (ValueTree::fromXml (*xml));
}

//==============================================================================
AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ChipSynthAudioProcessor();
}
