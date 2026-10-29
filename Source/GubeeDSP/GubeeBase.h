#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "GubeeLook.h"

namespace gubee
{
using APVTS = juce::AudioProcessorValueTreeState;

/** Parameter names may use "Section|Label" to group knobs into sections in the editor. */
inline std::unique_ptr<juce::AudioParameterFloat> fparam (const juce::String& id, const juce::String& name,
                                                          float lo, float hi, float def, float centre = -1.0f,
                                                          const juce::String& label = {})
{
    juce::NormalisableRange<float> r (lo, hi);
    if (centre > lo && centre < hi) r.setSkewForCentre (centre);
    auto fmt = [label] (float v, int)
    {
        const float a = std::abs (v);
        juce::String t = a >= 1000.0f && label == "Hz" ? juce::String (v / 1000.0f, a >= 10000.0f ? 1 : 2) + " k"
                       : juce::String (v, a >= 100.0f ? 0 : a >= 10.0f ? 1 : 2);
        if (label.isNotEmpty()) t << (t.endsWith (" k") ? "" : " ") << label;
        return t;
    };
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, r, def,
                                                        juce::AudioParameterFloatAttributes().withLabel (label).withStringFromValueFunction (fmt));
}

inline std::unique_ptr<juce::AudioParameterFloat> iparam (const juce::String& id, const juce::String& name, float lo, float hi, float def, const juce::String& label = {})
{
    return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (lo, hi, 1.0f), def,
                                                        juce::AudioParameterFloatAttributes().withLabel (label)
                                                            .withStringFromValueFunction ([label] (float v, int) { return juce::String ((int) std::round (v)) + (label.isNotEmpty() ? " " + label : juce::String()); }));
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

/** Tempo-sync helpers. */
inline juce::StringArray syncDivisions() { return { "1/32", "1/16 T", "1/16", "1/8 T", "1/16 D", "1/8", "1/4 T", "1/8 D", "1/4", "1/4 D", "1/2", "1 Bar", "2 Bars", "4 Bars" }; }
inline double syncBeats (int i)
{
    const double v[] = { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.375, 0.5, 2.0 / 3.0, 0.75, 1.0, 1.5, 2.0, 4.0, 8.0, 16.0 };
    return v[juce::jlimit (0, 13, i)];
}

struct Preset
{
    juce::String name;
    std::vector<std::pair<juce::String, float>> values;   // parameter id -> real value
};

/** Base class for every Gubee effect and instrument. */
class GubeeProcessor : public juce::AudioProcessor
{
public:
    GubeeProcessor (const juce::String& name, bool isSynth, APVTS::ParameterLayout layout, bool withSidechain = false)
        : juce::AudioProcessor (makeBuses (isSynth, withSidechain)),
          apvts (*this, nullptr, "PARAMS", std::move (layout)),
          pluginName (name), synth (isSynth), sidechain (withSidechain)
    {}

    static BusesProperties makeBuses (bool isSynth, bool withSidechain)
    {
        if (isSynth) return BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
        auto b = BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                  .withOutput ("Output", juce::AudioChannelSet::stereo(), true);
        if (withSidechain) b = b.withInput ("Sidechain", juce::AudioChannelSet::stereo(), false);
        return b;
    }

    const juce::String getName() const override             { return pluginName; }
    bool acceptsMidi() const override                        { return synth || wantsMidi; }
    bool producesMidi() const override                       { return false; }
    bool isMidiEffect() const override                       { return false; }
    double getTailLengthSeconds() const override             { return 2.0; }
    void releaseResources() override                         {}
    bool hasEditor() const override                          { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    bool isSynth() const                                     { return synth; }
    bool hasSidechainInput() const                           { return sidechain; }

    // ---- presets (factory presets double as host "programs")
    virtual std::vector<Preset> factoryPresets() const       { return {}; }
    int getNumPrograms() override                            { return juce::jmax (1, (int) factoryPresets().size()); }
    int getCurrentProgram() override                         { return currentProgram; }
    const juce::String getProgramName (int i) override
    {
        auto p = factoryPresets();
        return i >= 0 && i < (int) p.size() ? p[(size_t) i].name : juce::String ("Default");
    }
    void changeProgramName (int, const juce::String&) override {}
    void setCurrentProgram (int i) override
    {
        auto p = factoryPresets();
        if (i < 0 || i >= (int) p.size()) return;
        currentProgram = i;
        applyPreset (p[(size_t) i]);
    }
    void applyPreset (const Preset& pr)
    {
        for (auto* prm : getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
                rp->setValueNotifyingHost (rp->getDefaultValue());
        for (auto& [id, v] : pr.values)
            if (auto* rp = apvts.getParameter (id))
                rp->setValueNotifyingHost (rp->convertTo0to1 (v));
    }
    static juce::File userPresetFolder (const juce::String& plugin)
    {
        auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("GubeeLoops Presets").getChildFile (plugin);
        d.createDirectory();
        return d;
    }
    bool saveUserPreset (const juce::String& name)
    {
        if (auto xml = apvts.copyState().createXml())
            return xml->writeTo (userPresetFolder (getName()).getChildFile (juce::File::createLegalFileName (name) + ".gubeepreset"));
        return false;
    }
    bool loadUserPreset (const juce::File& f)
    {
        if (auto xml = juce::XmlDocument::parse (f))
            if (xml->hasTagName (apvts.state.getType())) { apvts.replaceState (juce::ValueTree::fromXml (*xml)); return true; }
        return false;
    }

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        auto out = layouts.getMainOutputChannelSet();
        if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
        if (synth) return true;
        if (layouts.getMainInputChannelSet() != out) return false;
        if (layouts.inputBuses.size() > 1)
        {
            auto sc = layouts.inputBuses[1];
            if (! sc.isDisabled() && sc != juce::AudioChannelSet::stereo() && sc != juce::AudioChannelSet::mono()) return false;
        }
        return true;
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

    /** Sidechain input (empty buffer when not connected). */
    juce::AudioBuffer<float> sidechainBuffer (juce::AudioBuffer<float>& b)
    {
        if (sidechain && getBusCount (true) > 1 && getBus (true, 1)->isEnabled() && getBus (true, 1)->getNumberOfChannels() > 0)
            return getBusBuffer (b, true, 1);
        return {};
    }

    /** Optional visual shown at the top of the editor (e.g. a scope). */
    virtual std::unique_ptr<juce::Component> createVisual() { return nullptr; }
    virtual int visualHeight() const { return 0; }

    APVTS apvts;

protected:
    double sr = 44100.0;
    bool wantsMidi = false;

private:
    juce::String pluginName;
    bool synth, sidechain;
    int currentProgram = 0;
};

//==============================================================================
/** Polished editor: header with preset bar, knobs grouped into section cards. */
class GubeeEditor : public juce::AudioProcessorEditor, private juce::Timer
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
            auto full = rp->getName (64);
            juce::String section = full.containsChar ('|') ? full.upToFirstOccurrenceOf ("|", false, false) : juce::String ("Controls");
            juce::String label = full.containsChar ('|') ? full.fromFirstOccurrenceOf ("|", false, false) : full;
            Section* sec = nullptr;
            for (auto& s : sections) if (s->title == section) sec = s.get();
            if (sec == nullptr) { sections.push_back (std::make_unique<Section>()); sec = sections.back().get(); sec->title = section; }

            auto item = std::make_unique<Item>();
            item->label.setText (label, juce::dontSendNotification);
            item->label.setJustificationType (juce::Justification::centred);
            item->label.setFont (juce::FontOptions (11.5f));
            item->label.setColour (juce::Label::textColourId, col::textDim);
            addAndMakeVisible (item->label);

            if (dynamic_cast<juce::AudioParameterChoice*> (rp) != nullptr)
            {
                auto* cb = new juce::ComboBox();
                cb->addItemList (rp->getAllValueStrings(), 1);
                item->comp.reset (cb);
                item->comboAtt = std::make_unique<APVTS::ComboBoxAttachment> (p.apvts, id, *cb);
                item->wide = true;
            }
            else if (dynamic_cast<juce::AudioParameterBool*> (rp) != nullptr)
            {
                auto* tb = new juce::TextButton (label);
                tb->setClickingTogglesState (true);
                tb->setColour (juce::TextButton::buttonOnColourId, col::accent);
                item->comp.reset (tb);
                item->buttonAtt = std::make_unique<APVTS::ButtonAttachment> (p.apvts, id, *tb);
                item->label.setVisible (false);
            }
            else
            {
                auto* s = new juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow);
                s->setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 15);
                s->setColour (juce::Slider::textBoxTextColourId, col::text);
                item->comp.reset (s);
                item->sliderAtt = std::make_unique<APVTS::SliderAttachment> (p.apvts, id, *s);
                s->setDoubleClickReturnValue (true, rp->convertFrom0to1 (rp->getDefaultValue()));
            }
            addAndMakeVisible (*item->comp);
            sec->items.push_back (std::move (item));
        }

        addAndMakeVisible (presetBox);
        presetBox.setTextWhenNothingSelected ("Presets");
        presetBox.onChange = [this] { choosePreset(); };
        for (auto* b : { &prevBtn, &nextBtn, &saveBtn }) addAndMakeVisible (*b);
        prevBtn.onClick = [this] { step (-1); };
        nextBtn.onClick = [this] { step (1); };
        saveBtn.setTooltip ("Save the current settings as your own preset");
        saveBtn.onClick = [this] { savePreset(); };
        refreshPresets();

        visual = p.createVisual();
        if (visual) addAndMakeVisible (*visual);
        layoutAndSize();
        startTimerHz (4);
    }

    ~GubeeEditor() override { sections.clear(); setLookAndFeel (nullptr); }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg);
        auto header = getLocalBounds().removeFromTop (headerH).toFloat();
        juce::ColourGradient grad (col::panelHi, 0, 0, col::panel, 0, (float) headerH, false);
        g.setGradientFill (grad);
        g.fillRect (header);
        g.setColour (col::outline);
        g.drawHorizontalLine (headerH - 1, 0, (float) getWidth());
        drawCatLogo (g, header.removeFromLeft ((float) headerH).reduced (7));
        g.setColour (col::text);
        g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
        g.drawText (proc.getName().fromFirstOccurrenceOf ("Gubee ", false, false).isNotEmpty() ? proc.getName().fromFirstOccurrenceOf ("Gubee ", false, false) : proc.getName(),
                    header.withWidth (260).translated (0, -7), juce::Justification::centredLeft);
        g.setColour (col::accent);
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawText ("GUBEE", header.withWidth (260).translated (1, 12), juce::Justification::centredLeft);

        for (auto& s : sections)
        {
            auto r = s->bounds.toFloat();
            g.setColour (col::panel);
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (col::outline.withAlpha (0.7f));
            g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
            g.setColour (col::textDim);
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawText (s->title.toUpperCase(), r.removeFromTop (22).reduced (12, 0), juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        auto top = getLocalBounds().removeFromTop (headerH).reduced (10, 13);
        saveBtn.setBounds (top.removeFromRight (54)); top.removeFromRight (6);
        nextBtn.setBounds (top.removeFromRight (26)); top.removeFromRight (2);
        prevBtn.setBounds (top.removeFromRight (26)); top.removeFromRight (4);
        presetBox.setBounds (top.removeFromRight (juce::jmin (210, top.getWidth() - 200)));
        if (visual) visual->setBounds (visualArea);
        for (auto& s : sections)
        {
            auto r = s->bounds.reduced (10, 0).withTrimmedTop (24).withTrimmedBottom (8);
            int x = r.getX();
            for (auto& it : s->items)
            {
                const int w = it->wide ? cellW + 24 : cellW;
                juce::Rectangle<int> cell (x, r.getY() + (it->rowIndex * cellH), w, cellH);
                if (it->rowStart) x = r.getX();
                cell.setX (r.getX() + it->col);
                if (auto* sl = dynamic_cast<juce::Slider*> (it->comp.get()))
                {
                    it->label.setBounds (cell.removeFromTop (16));
                    sl->setBounds (cell.reduced (4, 0));
                }
                else if (dynamic_cast<juce::TextButton*> (it->comp.get()) != nullptr)
                    it->comp->setBounds (cell.withSizeKeepingCentre (w - 10, 28));
                else
                {
                    it->label.setBounds (cell.removeFromTop (16));
                    it->comp->setBounds (cell.withSizeKeepingCentre (w - 8, 26).withY (cell.getY() + 8));
                }
                juce::ignoreUnused (x);
            }
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
        bool wide = false, rowStart = false;
        int col = 0, rowIndex = 0;
        ~Item() { sliderAtt.reset(); comboAtt.reset(); buttonAtt.reset(); }
    };
    struct Section
    {
        juce::String title;
        std::vector<std::unique_ptr<Item>> items;
        juce::Rectangle<int> bounds;
    };

    void layoutAndSize()
    {
        // each section lays out its items in rows of up to maxPerRow; sections flow left to right
        const int maxWidth = 1000;
        int x = pad, y = headerH + pad, rowH = 0, totalW = 0;
        if (visual) { visualArea = { pad, y, 0, proc.visualHeight() }; y += proc.visualHeight() + pad; }
        for (auto& s : sections)
        {
            const int n = (int) s->items.size();
            const int perRow = juce::jlimit (1, 8, n <= 8 ? n : (n + 1) / 2 > 8 ? 8 : (n + 1) / 2);
            int col = 0, row = 0, rowWidth = 0, secW = 0;
            for (auto& it : s->items)
            {
                const int w = it->wide ? cellW + 24 : cellW;
                if (col >= perRow) { col = 0; ++row; rowWidth = 0; }
                it->col = rowWidth; it->rowIndex = row;
                rowWidth += w; secW = juce::jmax (secW, rowWidth);
                ++col;
            }
            const int w = secW + 20, h = 24 + (row + 1) * cellH + 8;
            if (x + w > maxWidth + pad && x > pad) { x = pad; y += rowH + pad; rowH = 0; }
            s->bounds = { x, y, w, h };
            x += w + pad;
            rowH = juce::jmax (rowH, h);
            totalW = juce::jmax (totalW, x);
        }
        const int width = juce::jmax (460, totalW);
        if (visual) visualArea.setWidth (width - 2 * pad);
        setSize (width, y + rowH + pad);
    }

    void refreshPresets()
    {
        presetBox.clear (juce::dontSendNotification);
        factory = proc.factoryPresets();
        int id = 1;
        if (! factory.empty()) presetBox.addSectionHeading ("Factory");
        for (auto& f : factory) presetBox.addItem (f.name, id++);
        user = GubeeProcessor::userPresetFolder (proc.getName()).findChildFiles (juce::File::findFiles, false, "*.gubeepreset");
        user.sort();
        if (! user.isEmpty()) presetBox.addSectionHeading ("Yours");
        for (auto& f : user) presetBox.addItem (f.getFileNameWithoutExtension(), 1000 + user.indexOf (f));
    }

    void choosePreset()
    {
        int id = presetBox.getSelectedId();
        if (id >= 1000) proc.loadUserPreset (user[id - 1000]);
        else if (id >= 1 && id <= (int) factory.size()) proc.setCurrentProgram (id - 1);
    }

    void step (int d)
    {
        const int n = presetBox.getNumItems();
        if (n == 0) return;
        int idx = juce::jlimit (0, n - 1, presetBox.getSelectedItemIndex() + d);
        if (presetBox.getSelectedItemIndex() < 0) idx = 0;
        presetBox.setSelectedItemIndex (idx);
    }

    void savePreset()
    {
        auto w = std::make_shared<juce::AlertWindow> ("Save preset", "Name your preset", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("n", "My " + proc.getName().fromFirstOccurrenceOf ("Gubee ", false, false));
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<GubeeEditor> sp (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([w, sp] (int r)
        {
            if (r != 1 || sp == nullptr) return;
            auto name = w->getTextEditorContents ("n");
            if (name.trim().isEmpty()) return;
            sp->proc.saveUserPreset (name);
            sp->refreshPresets();
            for (int i = 0; i < sp->presetBox.getNumItems(); ++i)
                if (sp->presetBox.getItemText (i) == juce::File::createLegalFileName (name)) sp->presetBox.setSelectedItemIndex (i, juce::dontSendNotification);
        }), false);
    }

    void timerCallback() override {}

    static constexpr int headerH = 56, pad = 10, cellW = 76, cellH = 96;
    GubeeLookAndFeel lnf;
    GubeeProcessor& proc;
    std::vector<std::unique_ptr<Section>> sections;
    std::unique_ptr<juce::Component> visual;
    juce::Rectangle<int> visualArea;
    juce::ComboBox presetBox;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, saveBtn { "Save" };
    std::vector<Preset> factory;
    juce::Array<juce::File> user;
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

/** Host position in beats (or -1 when unknown / stopped). */
inline double hostBeats (juce::AudioProcessor& p)
{
    if (auto* ph = p.getPlayHead())
        if (auto pos = ph->getPosition())
            if (pos->getIsPlaying())
                if (auto ppq = pos->getPpqPosition()) return *ppq;
    return -1.0;
}

/** Fractional delay line. */
struct DelayLine
{
    std::vector<float> buf;
    int w = 0;
    void setSize (int n) { buf.assign ((size_t) juce::jmax (4, n), 0.0f); w = 0; }
    void clear() { std::fill (buf.begin(), buf.end(), 0.0f); }
    void push (float x) { buf[(size_t) w] = x; if (++w >= (int) buf.size()) w = 0; }
    float read (float d) const
    {
        const int n = (int) buf.size();
        float rp = (float) w - 1.0f - d;
        while (rp < 0) rp += (float) n;
        int i0 = (int) rp; float fr = rp - (float) i0;
        int i1 = (i0 + 1) % n;
        return buf[(size_t) i0] + fr * (buf[(size_t) i1] - buf[(size_t) i0]);
    }
};

/** Simple one-pole smoothing / envelope helpers. */
inline float coefFor (double seconds, double sr) { return seconds <= 0 ? 0.0f : (float) std::exp (-1.0 / (seconds * sr)); }
}
