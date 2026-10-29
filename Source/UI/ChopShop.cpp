#include "ChopShop.h"
#include "BrowserPanel.h"
#include "WaveCache.h"

namespace gubee
{
//==============================================================================
struct ChopShop::Wave : public juce::Component
{
    explicit Wave (ChopShop& o) : shop (o) {}
    ChopShop& shop;
    double viewStart = 0, viewLen = 1;   // in samples
    WaveCache::Peaks peaks;

    void rebuildPeaks()
    {
        if (shop.work == nullptr) return;
        WaveCache::build (peaks, shop.work->buffer, shop.work->sampleRate);
        viewStart = 0; viewLen = juce::jmax (1, shop.work->buffer.getNumSamples());
        repaint();
    }
    void refreshPeaks() { if (shop.work) WaveCache::build (peaks, shop.work->buffer, shop.work->sampleRate); repaint(); }

    int xToSample (int x) const { return (int) juce::jlimit (0.0, (double) total(), viewStart + x * viewLen / juce::jmax (1, getWidth())); }
    int sampleToX (double s) const { return (int) ((s - viewStart) * getWidth() / viewLen); }
    int total() const { return shop.work ? shop.work->buffer.getNumSamples() : 0; }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (col::bg.darker (0.2f));
        if (shop.work == nullptr)
        {
            g.setColour (col::textDim);
            g.setFont (juce::FontOptions (15.0f));
            g.drawText ("Drag a sound here from the browser, or double-click an audio clip / right-click a sampler channel",
                        getLocalBounds(), juce::Justification::centred);
            return;
        }
        auto r = getLocalBounds().toFloat();
        // slices shading
        auto rs = shop.ranges();
        for (size_t i = 0; i < rs.size(); ++i)
        {
            g.setColour (col::fromIndex ((int) i).withAlpha (0.10f));
            g.fillRect (juce::Rectangle<float> ((float) sampleToX (rs[i].getStart()), 0, (float) (sampleToX (rs[i].getEnd()) - sampleToX (rs[i].getStart())), r.getHeight()));
        }
        if (! shop.selection.isEmpty())
        {
            g.setColour (col::accent.withAlpha (0.18f));
            g.fillRect (juce::Rectangle<float> ((float) sampleToX (shop.selection.getStart()), 0,
                                                (float) (sampleToX (shop.selection.getEnd()) - sampleToX (shop.selection.getStart())), r.getHeight()));
        }
        g.setColour (col::catGray.brighter (0.3f));
        WaveCache::draw (g, r.reduced (0, 8), peaks, viewStart, viewStart + viewLen);
        g.setColour (col::outline);
        g.drawHorizontalLine ((int) r.getCentreY(), 0, r.getWidth());
        // markers
        for (size_t i = 0; i < shop.markers.size(); ++i)
        {
            int x = sampleToX (shop.markers[i]);
            if (x < -20 || x > getWidth() + 20) continue;
            g.setColour (col::accent2);
            g.drawVerticalLine (x, 0, r.getHeight());
            juce::Rectangle<float> tag ((float) x, 0, 22, 16);
            g.fillRoundedRectangle (tag, 3);
            g.setColour (col::bg);
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (juce::String ((int) i + 1), tag, juce::Justification::centred);
        }
        // cursor + preview position
        g.setColour (juce::Colours::white.withAlpha (0.6f));
        g.drawVerticalLine (sampleToX (shop.cursor), 0, r.getHeight());
        double pp = shop.ctx.engine->previewPositionSeconds();
        if (pp >= 0)
        {
            g.setColour (col::accent);
            g.drawVerticalLine (sampleToX (pp * shop.work->sampleRate), 0, r.getHeight());
        }
        g.setColour (col::textDim);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (shop.source.getFileName() + "   " + juce::String (shop.work->lengthSeconds(), 3) + " s   " +
                    juce::String ((int) shop.markers.size()) + " chops", getLocalBounds().reduced (6, 4), juce::Justification::bottomLeft);
    }

    int markerAt (int x) const
    {
        for (size_t i = 0; i < shop.markers.size(); ++i) if (std::abs (sampleToX (shop.markers[i]) - x) < 5) return (int) i;
        return -1;
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (shop.work == nullptr) return;
        shop.grabKeyboardFocus();
        int m = markerAt (e.x);
        if (e.mods.isPopupMenu())
        {
            if (m >= 0) { shop.markers.erase (shop.markers.begin() + m); repaint(); shop.resized(); }
            return;
        }
        if (e.y < 18 && m >= 0) { shop.playSlice (m); return; }
        if (m >= 0) { dragMarker = m; return; }
        dragMarker = -1;
        anchor = xToSample (e.x);
        shop.cursor = anchor;
        shop.selection = {};
        repaint();
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (shop.work == nullptr) return;
        if (dragMarker >= 0) { shop.markers[(size_t) dragMarker] = xToSample (e.x); repaint(); return; }
        int s = xToSample (e.x);
        shop.selection = { juce::jmin (anchor, s), juce::jmax (anchor, s) };
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragMarker >= 0) { std::sort (shop.markers.begin(), shop.markers.end()); dragMarker = -1; repaint(); }
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (shop.work == nullptr) return;
        shop.markers.push_back (xToSample (e.x));
        std::sort (shop.markers.begin(), shop.markers.end());
        shop.resized();
        repaint();
    }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (shop.work == nullptr) return;
        if (e.mods.isCtrlDown() || std::abs (w.deltaY) > std::abs (w.deltaX))
        {
            double centre = xToSample (e.x);
            double f = w.deltaY > 0 ? 0.8 : 1.25;
            viewLen = juce::jlimit (64.0, (double) total(), viewLen * f);
            viewStart = juce::jlimit (0.0, juce::jmax (0.0, total() - viewLen), centre - (centre - viewStart) * f);
        }
        else viewStart = juce::jlimit (0.0, juce::jmax (0.0, total() - viewLen), viewStart - w.deltaX * viewLen * 0.3);
        repaint();
    }
    int dragMarker = -1, anchor = 0;
};

//==============================================================================
ChopShop::ChopShop (AppContext& c) : ctx (c)
{
    wave = std::make_unique<Wave> (*this);
    addAndMakeVisible (*wave);
    for (auto* b : { &playBtn, &stopBtn, &trimBtn, &delBtn, &silenceBtn, &revBtn, &fadeInBtn, &fadeOutBtn, &normBtn, &gainUp, &gainDown,
                     &copyBtn, &pasteBtn, &undoBtn, &stretchBtn, &pitchBtn, &detectBtn, &clearBtn, &lazyBtn, &toPads, &toSampler,
                     &exportBtn, &saveBtn, &applyBtn, &openBtn })
        addAndMakeVisible (*b);

    playBtn.onClick = [this] { if (work) ctx.engine->previewBuffer (work, selection.isEmpty() ? juce::Range<int> (cursor, work->buffer.getNumSamples()) : selection); };
    stopBtn.onClick = [this] { ctx.engine->stopPreview(); };
    trimBtn.setTooltip ("Keep only the selection");
    trimBtn.onClick = [this] { if (! work || selection.isEmpty()) return; pushUndo(); work->buffer = tools::extract (work->buffer, selection); markers.clear(); selection = {}; cursor = 0; edited(); wave->rebuildPeaks(); };
    delBtn.onClick = [this] { if (! work || selection.isEmpty()) return; pushUndo(); clipboard = tools::extract (work->buffer, selection); work->buffer = tools::removeRange (work->buffer, selection); cursor = selection.getStart(); selection = {}; markers.clear(); edited(); wave->rebuildPeaks(); };
    silenceBtn.onClick = [this] { if (! work || selection.isEmpty()) return; pushUndo(); tools::gainRange (work->buffer, selection, 0.0f); edited(); };
    revBtn.onClick = [this] { if (! work) return; pushUndo(); tools::reverseRange (work->buffer, selOrAll()); edited(); };
    fadeInBtn.onClick = [this] { if (! work) return; pushUndo(); tools::fadeRange (work->buffer, selOrAll(), true); edited(); };
    fadeOutBtn.onClick = [this] { if (! work) return; pushUndo(); tools::fadeRange (work->buffer, selOrAll(), false); edited(); };
    normBtn.onClick = [this] { if (! work) return; pushUndo(); tools::normalize (work->buffer, selOrAll()); edited(); };
    gainUp.onClick = [this] { if (! work) return; pushUndo(); tools::gainRange (work->buffer, selOrAll(), 1.4125f); edited(); };
    gainDown.onClick = [this] { if (! work) return; pushUndo(); tools::gainRange (work->buffer, selOrAll(), 0.708f); edited(); };
    copyBtn.onClick = [this] { if (work) clipboard = tools::extract (work->buffer, selOrAll()); };
    pasteBtn.onClick = [this] { if (! work || clipboard.getNumSamples() == 0) return; pushUndo(); work->buffer = tools::insertAt (work->buffer, cursor, clipboard); markers.clear(); edited(); wave->rebuildPeaks(); };
    undoBtn.onClick = [this]
    {
        if (! work || undoStack.empty()) return;
        work->buffer = std::move (undoStack.back());
        undoStack.pop_back();
        markers.clear(); selection = {};
        edited(); wave->rebuildPeaks();
    };

    addAndMakeVisible (bpmLabel);
    bpmLabel.setText ("Original BPM", juce::dontSendNotification);
    addAndMakeVisible (bpmEdit);
    bpmEdit.onTextChange = [this] { bpm = bpmEdit.getText().getDoubleValue(); };
    stretchBtn.setTooltip ("Time-stretch so this loop plays at the project tempo without changing pitch");
    stretchBtn.onClick = [this] { stretchToTempo(); };
    addAndMakeVisible (pitchBox);
    for (int s = -12; s <= 12; ++s) pitchBox.addItem ((s > 0 ? "+" : "") + juce::String (s) + " st", s + 13);
    pitchBox.setSelectedId (13);
    pitchBtn.setTooltip ("Shift the pitch without changing the length");
    pitchBtn.onClick = [this] { applyPitch(); };

    addAndMakeVisible (modeBox);
    modeBox.addItemList ({ "By transients (hits)", "Equal regions", "By beat grid", "Manual (double-click)" }, 1);
    modeBox.setSelectedId (1);
    modeBox.onChange = [this]
    {
        amountBox.clear (juce::dontSendNotification);
        int m = modeBox.getSelectedId();
        if (m == 2) { amountBox.addItemList ({ "2", "4", "8", "16", "32" }, 1); amountBox.setSelectedId (4); }
        else if (m == 3) { amountBox.addItemList ({ "1 bar", "1/2 bar", "1 beat", "1/8", "1/16" }, 1); amountBox.setSelectedId (3); }
        sensitivity.setVisible (m == 1);
        amountBox.setVisible (m == 2 || m == 3);
    };
    addAndMakeVisible (amountBox);
    addChildComponent (sensitivity);
    sensitivity.setRange (0, 1, 0.01);
    sensitivity.setValue (0.6);
    sensitivity.setTooltip ("Sensitivity: higher finds more hits");
    sensitivity.setVisible (true);
    amountBox.setVisible (false);
    detectBtn.setColour (juce::TextButton::buttonColourId, col::accent.withAlpha (0.4f));
    detectBtn.onClick = [this] { detect(); };
    clearBtn.onClick = [this] { markers.clear(); resized(); wave->repaint(); };
    lazyBtn.setClickingTogglesState (true);
    lazyBtn.setColour (juce::TextButton::buttonOnColourId, col::accent2);
    lazyBtn.setTooltip ("Lazy Chop (MPC style): plays the sample - press L or the spacebar at each spot you want a chop");
    lazyBtn.onClick = [this]
    {
        lazyChop = lazyBtn.getToggleState();
        if (lazyChop && work) { markers.clear(); markers.push_back (0); ctx.engine->previewBuffer (work); grabKeyboardFocus(); }
        else ctx.engine->stopPreview();
        resized();
    };
    toPads.setTooltip ("Make a new Drum Pads channel with one chop per pad, plus a pattern that plays them in order");
    toPads.onClick = [this] { slicesToPads(); };
    toSampler.setTooltip ("Make a Sampler where each key from C4 up plays one chop");
    toSampler.onClick = [this] { slicesToSampler(); };
    exportBtn.onClick = [this] { exportSlices(); };
    saveBtn.setTooltip ("Save the edited sound into Library/My Chops (shows up in the browser)");
    saveBtn.onClick = [this] { auto f = saveWorking (""); if (f.existsAsFile()) ctx.status ("Saved " + f.getFileName()); };
    applyBtn.setTooltip ("Save the edit and use it in the channel / clip you opened it from");
    applyBtn.onClick = [this] { applyToSource(); };
    openBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Open audio", paths::library(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc) { if (fc.getResult().existsAsFile()) load (fc.getResult(), -1, {}); });
    };

    setWantsKeyboardFocus (true);
    startTimerHz (30);
}

ChopShop::~ChopShop() = default;

void ChopShop::load (const juce::File& f, int channelId, juce::ValueTree clip)
{
    auto data = ctx.samples.get (f);
    if (data == nullptr) { ctx.status ("Couldn't open " + f.getFileName()); return; }
    source = f; sourceChannel = channelId; sourceClip = clip;
    work = std::make_shared<SampleData> (*data);
    undoStack.clear();
    markers.clear();
    if (channelId >= 0)
    {
        auto ch = ctx.project.findChannel (channelId);
        for (auto& r : Project::slicesFromString (ch.getProperty (ids::slices).toString())) markers.push_back (r.getStart());
    }
    selection = {}; cursor = 0;
    bpm = tools::bpmFromName (f.getFileNameWithoutExtension());
    if (bpm <= 0) bpm = tools::guessLoopBpm (data->lengthSeconds());
    if (clip.isValid() && (double) clip.getProperty (ids::srcBpm, 0.0) > 0) bpm = clip.getProperty (ids::srcBpm);
    bpmEdit.setText (bpm > 0 ? juce::String (bpm, 2) : juce::String(), juce::dontSendNotification);
    applyBtn.setEnabled (channelId >= 0 || clip.isValid());
    wave->rebuildPeaks();
    resized();
    repaint();
}

void ChopShop::pushUndo()
{
    if (work == nullptr) return;
    ctx.engine->stopPreview();
    undoStack.push_back (work->buffer);
    if (undoStack.size() > 30) undoStack.erase (undoStack.begin());
}

void ChopShop::edited()
{
    // give the preview a fresh immutable copy
    work = std::make_shared<SampleData> (*work);
    wave->refreshPeaks();
}

juce::Range<int> ChopShop::selOrAll() const
{
    return selection.isEmpty() ? juce::Range<int> (0, work ? work->buffer.getNumSamples() : 0) : selection;
}

std::vector<juce::Range<int>> ChopShop::ranges() const
{
    if (work == nullptr || markers.empty()) return {};
    auto pts = markers;
    if (pts.front() > 0) pts.insert (pts.begin(), 0);
    return tools::pointsToRanges (pts, work->buffer.getNumSamples());
}

void ChopShop::detect()
{
    if (work == nullptr) return;
    const int m = modeBox.getSelectedId();
    const int n = work->buffer.getNumSamples();
    if (m == 1) markers = tools::detectTransients (work->buffer, work->sampleRate, (float) sensitivity.getValue());
    else if (m == 2) markers = tools::equalSlices (n, amountBox.getText().getIntValue());
    else if (m == 3)
    {
        if (bpm <= 0) { ctx.status ("Set the original BPM first"); return; }
        const double beats[] = { 4.0, 2.0, 1.0, 0.5, 0.25 };
        markers = tools::gridSlices (n, work->sampleRate, bpm, beats[juce::jlimit (0, 4, amountBox.getSelectedItemIndex())]);
    }
    else { ctx.status ("Manual mode: double-click the waveform to add chops, right-click a chop to remove it"); return; }
    ctx.status (juce::String ((int) markers.size()) + " chops - click the numbered pads (or keys 1-9) to hear them");
    resized();
    wave->repaint();
}

void ChopShop::playSlice (int i)
{
    auto rs = ranges();
    if (work == nullptr || i < 0 || i >= (int) rs.size()) return;
    ctx.engine->previewBuffer (work, rs[(size_t) i]);
}

juce::File ChopShop::saveWorking (const juce::String& suffix)
{
    if (work == nullptr) return {};
    auto f = tools::uniqueFile (paths::chops(), source.getFileNameWithoutExtension() + suffix, ".wav");
    if (! tools::writeWav (f, work->buffer, work->sampleRate)) { ctx.status ("Couldn't save"); return {}; }
    ctx.library.rescan();
    return f;
}

void ChopShop::slicesToPads()
{
    auto rs = ranges();
    if (work == nullptr || rs.empty()) { ctx.status ("Chop the sample first"); return; }
    auto f = undoStack.empty() ? source : saveWorking (" edit");
    if (! f.existsAsFile()) return;
    auto& pr = ctx.project;
    pr.beginEdit ("Chops to pads");
    auto ch = ctx.addPadsChannel (source.getFileNameWithoutExtension() + " chops", false);
    const int id = (int) ch.getProperty (ids::id);
    auto pat = pr.addPattern (source.getFileNameWithoutExtension() + " chops");
    const int pid = (int) pat.getProperty (ids::id);
    const double secPerBeat = 60.0 / (bpm > 0 ? bpm : pr.tempo());
    double lastEnd = 0;
    for (size_t i = 0; i < rs.size() && i < 16; ++i)
    {
        pr.setPadSample (id, (int) i, f, rs[i]);
        const double beat = rs[i].getStart() / work->sampleRate / secPerBeat;
        const double len = rs[i].getLength() / work->sampleRate / secPerBeat;
        pr.addNote (pid, id, std::round (beat * 16.0) / 16.0, juce::jmax (0.0625, len), DrumPadsProcessor::baseNote + (int) i, 0.8f);
        lastEnd = juce::jmax (lastEnd, beat + len);
    }
    pat.setProperty (ids::length, juce::jmax (4.0, std::ceil (lastEnd / 4.0 - 1.0e-6) * 4.0), pr.um());
    if (bpm > 0 && std::abs (bpm - pr.tempo()) > 0.5)
        ctx.status ("Tip: the sample is " + juce::String (bpm, 1) + " BPM - set the project tempo to match, or stretch it first");
    ctx.selectChannel (id);
    ctx.setCurrentPattern (pid);
    if (ctx.showView) ctx.showView (viewRack);
    if (rs.size() > 16) ctx.status ("Only the first 16 chops fit on the pads");
}

void ChopShop::slicesToSampler()
{
    auto rs = ranges();
    if (work == nullptr || rs.empty()) { ctx.status ("Chop the sample first"); return; }
    auto f = undoStack.empty() ? source : saveWorking (" edit");
    if (! f.existsAsFile()) return;
    ctx.project.beginEdit ("Chops to sampler");
    auto ch = ctx.addSamplerChannel (f);
    ch.setProperty (ids::name, source.getFileNameWithoutExtension() + " keys", nullptr);
    ch.setProperty (ids::slices, Project::slicesToString (rs), nullptr);
    ctx.selectChannel ((int) ch.getProperty (ids::id));
    ctx.status ("Each key from C4 up plays one chop - try it in the Piano Roll");
    if (ctx.showView) ctx.showView (viewPiano);
}

void ChopShop::exportSlices()
{
    auto rs = ranges();
    if (work == nullptr || rs.empty()) { ctx.status ("Chop the sample first"); return; }
    auto dir = tools::uniqueFile (paths::chops(), source.getFileNameWithoutExtension() + " chops", "");
    dir.createDirectory();
    for (size_t i = 0; i < rs.size(); ++i)
        tools::writeWav (dir.getChildFile (source.getFileNameWithoutExtension() + " chop " + juce::String ((int) i + 1).paddedLeft ('0', 2) + ".wav"),
                         tools::extract (work->buffer, rs[i]), work->sampleRate);
    ctx.library.rescan();
    ctx.status ("Exported " + juce::String ((int) rs.size()) + " chops to " + dir.getFileName());
}

void ChopShop::applyToSource()
{
    if (work == nullptr) return;
    auto f = undoStack.empty() ? source : saveWorking (" edit");
    if (! f.existsAsFile()) return;
    auto& pr = ctx.project;
    pr.beginEdit ("Apply chop edit");
    if (sourceChannel >= 0)
    {
        auto ch = pr.findChannel (sourceChannel);
        ch.setProperty (ids::sample, f.getFullPathName(), pr.um());
        ch.setProperty (ids::slices, Project::slicesToString (ranges()), pr.um());
        ctx.status (ranges().empty() ? "Sampler updated" : "Sampler now plays chops on keys from C4 up");
    }
    else if (sourceClip.isValid())
    {
        sourceClip.setProperty (ids::file, f.getFullPathName(), pr.um());
        sourceClip.setProperty (ids::offset, 0.0, pr.um());
        sourceClip.setProperty (ids::reverse, false, pr.um());
        if (bpm > 0) sourceClip.setProperty (ids::srcBpm, bpm, pr.um());
        ctx.status ("Clip updated");
    }
    source = f;
    undoStack.clear();
}

void ChopShop::stretchToTempo()
{
    if (work == nullptr) return;
    if (bpm <= 0) { ctx.status ("Type the original BPM of this sample first"); return; }
    pushUndo();
    const double factor = bpm / ctx.project.tempo();
    work->buffer = tools::timeStretch (work->buffer, factor, work->sampleRate);
    for (auto& m : markers) m = (int) (m * factor);
    bpm = ctx.project.tempo();
    bpmEdit.setText (juce::String (bpm, 2), juce::dontSendNotification);
    edited(); wave->rebuildPeaks();
    ctx.status ("Stretched to " + juce::String (bpm, 1) + " BPM");
}

void ChopShop::applyPitch()
{
    if (work == nullptr) return;
    const int semis = pitchBox.getSelectedId() - 13;
    if (semis == 0) return;
    pushUndo();
    work->buffer = tools::stretchAndPitch (work->buffer, work->sampleRate, work->sampleRate, 1.0, semis);
    edited(); wave->rebuildPeaks();
    ctx.status ("Pitched " + juce::String (semis) + " semitones");
}

void ChopShop::timerCallback()
{
    if (ctx.engine->previewPositionSeconds() >= 0 || lazyChop) wave->repaint();
    if (lazyChop && ctx.engine->previewPositionSeconds() < 0) { lazyChop = false; lazyBtn.setToggleState (false, juce::dontSendNotification); resized(); }
}

bool ChopShop::keyPressed (const juce::KeyPress& k)
{
    const auto c = k.getTextCharacter();
    if (lazyChop && (c == 'l' || c == 'L' || k == juce::KeyPress::spaceKey))
    {
        double p = ctx.engine->previewPositionSeconds();
        if (p >= 0 && work) { markers.push_back ((int) (p * work->sampleRate)); std::sort (markers.begin(), markers.end()); resized(); wave->repaint(); }
        return true;
    }
    if (c >= '1' && c <= '9') { playSlice (c - '1'); return true; }
    if (c == '0') { playSlice (9); return true; }
    if (k == juce::KeyPress ('z', juce::ModifierKeys::ctrlModifier, 0)) { undoBtn.triggerClick(); return true; }
    if (k == juce::KeyPress::deleteKey) { delBtn.triggerClick(); return true; }
    return false;
}

void ChopShop::itemDropped (const SourceDetails& d)
{
    auto f = fileFromDrag (d.description);
    if (f.existsAsFile()) load (f, -1, {});
}

void ChopShop::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    drawViewHeader (g, getLocalBounds().removeFromTop (36).withTrimmedRight (100), "Chop Shop", "Drag to select  -  double-click adds a chop  -  drag markers to move  -  right-click removes  -  keys 1-9 play chops");
    if (lazyChop)
    {
        g.setColour (col::accent2);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText ("LAZY CHOP: press L (or Space) at each spot you want a chop", wave->getBounds().withHeight (24).translated (0, 20), juce::Justification::centred);
    }
}

void ChopShop::resized()
{
    auto r = getLocalBounds();
    auto head = r.removeFromTop (36).reduced (6, 5);
    openBtn.setBounds (head.removeFromRight (90));

    auto rowA = r.removeFromTop (34).reduced (6, 4);
    for (auto* b : { &playBtn, &stopBtn, &undoBtn }) { b->setBounds (rowA.removeFromLeft (54)); rowA.removeFromLeft (3); }
    rowA.removeFromLeft (10);
    for (auto* b : { &trimBtn, &delBtn, &copyBtn, &pasteBtn, &silenceBtn, &revBtn, &fadeInBtn, &fadeOutBtn, &normBtn, &gainUp, &gainDown })
    { b->setBounds (rowA.removeFromLeft (70)); rowA.removeFromLeft (3); }

    auto rowB = r.removeFromTop (34).reduced (6, 4);
    bpmLabel.setBounds (rowB.removeFromLeft (84));
    bpmEdit.setBounds (rowB.removeFromLeft (60)); rowB.removeFromLeft (4);
    stretchBtn.setBounds (rowB.removeFromLeft (180)); rowB.removeFromLeft (16);
    pitchBox.setBounds (rowB.removeFromLeft (80)); rowB.removeFromLeft (4);
    pitchBtn.setBounds (rowB.removeFromLeft (90)); rowB.removeFromLeft (16);
    applyBtn.setBounds (rowB.removeFromRight (120)); rowB.removeFromRight (4);
    saveBtn.setBounds (rowB.removeFromRight (140));

    auto rowC = r.removeFromTop (34).reduced (6, 4);
    modeBox.setBounds (rowC.removeFromLeft (170)); rowC.removeFromLeft (4);
    sensitivity.setBounds (rowC.withWidth (130)); amountBox.setBounds (rowC.removeFromLeft (130)); rowC.removeFromLeft (4);
    detectBtn.setBounds (rowC.removeFromLeft (70)); rowC.removeFromLeft (4);
    lazyBtn.setBounds (rowC.removeFromLeft (90)); rowC.removeFromLeft (4);
    clearBtn.setBounds (rowC.removeFromLeft (90)); rowC.removeFromLeft (16);
    toPads.setBounds (rowC.removeFromLeft (150)); rowC.removeFromLeft (4);
    toSampler.setBounds (rowC.removeFromLeft (160)); rowC.removeFromLeft (4);
    exportBtn.setBounds (rowC.removeFromLeft (110));

    // MPC-style pads for the chops
    auto padRow = r.removeFromBottom (64).reduced (6, 6);
    auto rs = ranges();
    const int n = juce::jmin (16, (int) rs.size());
    while (pads.size() < 16)
    {
        auto* b = pads.add (new juce::TextButton());
        int idx = pads.size() - 1;
        b->onClick = [this, idx] { playSlice (idx); };
        addChildComponent (b);
    }
    const int pw = juce::jmin (80, padRow.getWidth() / 16);
    for (int i = 0; i < 16; ++i)
    {
        pads[i]->setVisible (i < n);
        pads[i]->setButtonText (juce::String (i + 1));
        pads[i]->setColour (juce::TextButton::buttonColourId, col::fromIndex (i).withAlpha (0.55f));
        pads[i]->setBounds (padRow.getX() + i * pw, padRow.getY(), pw - 4, padRow.getHeight());
    }
    wave->setBounds (r.reduced (6, 2));
}
}
