#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

// A tiny monophonic chiptune synth: NES-style pulse / triangle / noise,
// ADSR, vibrato, pitch sweep, arpeggiator and a bit-crusher.
class ChipSynthAudioProcessor : public juce::AudioProcessor
{
public:
    ChipSynthAudioProcessor();
    ~ChipSynthAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ChipSynth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 3.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    struct Params
    {
        int   wave = 0, duty = 1, arpMode = 0;
        float attackMs = 0.f, decayMs = 300.f, sustain = 0.6f, releaseMs = 120.f;
        float vibRate = 6.f, vibDepth = 0.f, sweep = 0.f, arpRate = 15.f;
        float bits = 16.f, downsample = 1.f, volume = 0.5f;
    };

    enum class Stage { Idle, Attack, Decay, Sustain, Release };

    void  readParams();
    void  render (juce::AudioBuffer<float>& buffer, int start, int end);
    void  handleMidi (const juce::MidiMessage& m);
    void  noteOn (int note, float vel);
    void  noteOff (int note);
    void  beginRelease();
    void  rebuildArpNotes();
    void  stepEnvelope();
    float nextSample();

    juce::AudioProcessorValueTreeState apvts;
    Params p;

    double sr = 44100.0;

    // note state
    std::vector<int> held, arpNotes;
    int   currentNote = 60;
    float velocity = 1.f;

    // envelope
    Stage stage = Stage::Idle;
    float env = 0.f, releaseStep = 0.f;

    // oscillator / modulation state
    double   phase = 0.0, vibPhase = 0.0, arpCounter = 0.0, noiseAcc = 0.0, crushCounter = 0.0;
    float    sweepSemis = 0.f, crushHeld = 0.f;
    int      arpStep = 0;
    uint16_t lfsr = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChipSynthAudioProcessor)
};
