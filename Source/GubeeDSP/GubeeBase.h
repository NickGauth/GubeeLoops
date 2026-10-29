#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "GubeeLook.h"

namespace gubee
{
using APVTS = juce::AudioProcessorValueTreeState;

inline std::unique_ptr<juce::AudioParameterFloat> fparam (const juce::String& id, const juce::String& name,
                                                          float lo, float hi, float def, float centre = -1.0f,
                                                          const juce::String& label = {})
{
    juce::NormalisableRange<float> r (lo, hi);
    if (centre > lo && centre < hi) r.setSkewForCentre (centre);
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def,
                                                        juce::AudioParameterFloatAttributes().withLabel (label));
}

inline std::unique_ptr<juce::AudioParameterChoice> cparam (const juce::String& id, const juce::String& name,
                                                           const juce::StringArray& choices, int def)
{
    return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, choices, def);
}

inline std::unique_ptr<juce::AudioParameterBool> bparam (const juce::String& id, const juce::String& name, bool def)
{
    return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def);
}

/** Base class for every Gubee effect and instrument. */
class GubeeProcessor : public juce::AudioProcessor
{
public:
    GubeeProcessor (const juce::String& name, bool isSynth, APVTS::ParameterLayout layout)
        : juce::AudioProcessor (isSynth ? BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                        : BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "PARAMS", std::move (layout)),
          pluginName (name), synth (isSynth)
    {}

    const juce::String getName() const override             { return pluginName; }
    bool acceptsMidi() const override                        { return synth; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 2.0; }
    int getNumPrograms() override                            { return 1; }
    int getCurrentProgram() override                         { return 0; }
    void setCurrentProgram (int) override                    {}
    const juce::String getProgramName (int) override         { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}
    void releaseResources() override                         {}
    bool hasEditor() const override                          { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    bool isSynth() const                                     { return synth; }

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        auto out = layouts.getMainOutputChannelSet();
        if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
        return synth || layouts.getMainInputChannelSet() == out;
    }

    void getStateInformation (juce::MemoryBlock& dest) override
    {
        if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
    }

    void setStateInformation (const void* data, int size) override
    {
        if (auto xml = getXmlFromBinary (data, size))
            if (xml->hasTagName (apvts.state.getType()))
                apvts.replaceState (juce::ValueTree::fromXml (*xml));
    }

    float p (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    APVTS apvts;

protected:
    double sr = 44100.0;

private:
    juce::String pluginName;
    bool synth;
};

/** Generic knob-grid editor with the cat logo header. */
class GubeeEditor : public juce::AudioProcessorEditor
{
public:
    explicit GubeeEditor (GubeeProcessor& p) : juce::AudioProcessorEditor (p), proc (p)
    {
        setLookAndFeel (&lnf);
        for (auto* param : p.getParameters())
        {
            auto* rp = dynamic_cast<juce::RangedAudioParameter*> (param);
            if (rp == nullptr) continue;
            auto id = rp->getParameterID();
            auto item = std::make_unique<Item>();
            item->label.setText (rp->getName (20), juce::dontSendNotification);
            item->label.setJustificationType (juce::Justification::centred);
            item->label.setFont (juce::FontOptions (12.0f));
            addAndMakeVisible (item->label);

            if (dynamic_cast<juce::AudioParameterChoice*> (rp) != nullptr)
            {
                auto* cb = new juce::ComboBox();
                cb->addItemList (rp->getAllValueStrings(), 1);
                item->comp.reset (cb);
                item->comboAtt = std::make_unique<APVTS::ComboBoxAttachment> (p.apvts, id, *cb);
            }
            else if (dynamic_cast<juce::AudioParameterBool*> (rp) != nullptr)
            {
                auto* tb = new juce::ToggleButton ("On");
                item->comp.reset (tb);
                item->buttonAtt = std::make_unique<APVTS::ButtonAttachment> (p.apvts, id, *tb);
            }
            else
            {
                auto* s = new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
                s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
                item->comp.reset (s);
                item->sliderAtt = std::make_unique<APVTS::SliderAttachment> (p.apvts, id, *s);
                s->setTextValueSuffix (rp->getLabel().isNotEmpty() ? " " + rp->getLabel() : juce::String());
            }
            addAndMakeVisible (*item->comp);
            items.push_back (std::move (item));
        }
        int n = (int) items.size();
        cols = juce::jlimit (1, 5, n);
        int rows = (n + cols - 1) / cols;
        setSize (juce::jmax (300, cols * 96 + 24), 64 + rows * 116 + 10);
    }

    ~GubeeEditor() override { items.clear(); setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg);
        auto header = getLocalBounds().removeFromTop (52).toFloat();
        g.setColour (col::panel);
        g.fillRect (header);
        drawCatLogo (g, header.removeFromLeft (52).reduced (4));
        g.setColour (col::text);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText (proc.getName(), header.reduced (6, 0), juce::Justification::centredLeft);
        g.setColour (col::textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("GubeeLoops", header.reduced (10, 0), juce::Justification::centredRight);
    }

    void resized() override
    {
        auto area = getLocalBounds().withTrimmedTop (60).reduced (12, 4);
        for (size_t i = 0; i < items.size(); ++i)
        {
            int c = (int) i % cols, r = (int) i / cols;
            juce::Rectangle<int> cell (area.getX() + c * 96, area.getY() + r * 116, 92, 112);
            items[i]->label.setBounds (cell.removeFromTop (18));
            if (dynamic_cast<juce::Slider*> (items[i]->comp.get()) != nullptr)
                items[i]->comp->setBounds (cell);
            else
                items[i]->comp->setBounds (cell.withSizeKeepingCentre (86, 26));
        }
    }

private:
    struct Item
    {
        juce::Label label;
        std::unique_ptr<juce::Component> comp;
        std::unique_ptr<APVTS::SliderAttachment> sliderAtt;
        std::unique_ptr<APVTS::ComboBoxAttachment> comboAtt;
        std::unique_ptr<APVTS::ButtonAttachment> buttonAtt;
        ~Item() { sliderAtt.reset(); comboAtt.reset(); buttonAtt.reset(); }
    };
    GubeeLookAndFeel lnf;
    GubeeProcessor& proc;
    std::vector<std::unique_ptr<Item>> items;
    int cols = 4;
};

inline juce::AudioProcessorEditor* GubeeProcessor::createEditor() { return new GubeeEditor (*this); }

/** Helper: current host tempo or a default. */
inline double hostBpm (juce::AudioProcessor& p, double fallback = 120.0)
{
    if (auto* ph = p.getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm()) return *bpm;
    return fallback;
}
}
