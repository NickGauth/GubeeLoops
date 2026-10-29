#pragma once
#include "AppContext.h"

namespace gubee
{
/** Builds a menu of everything that can be automated / modulated. Returned ids map into `targets`. */
inline void buildTargetMenu (AppContext& ctx, juce::PopupMenu& menu, std::vector<std::pair<juce::String, juce::String>>& targets, int baseId)
{
    auto add = [&] (juce::PopupMenu& m, const juce::String& target, const juce::String& label)
    {
        targets.push_back ({ target, label });
        m.addItem (baseId + (int) targets.size() - 1, label);
    };
    juce::PopupMenu chans;
    for (auto c : ctx.project.channels())
    {
        const int id = (int) c.getProperty (ids::id);
        const auto name = c.getProperty (ids::name).toString();
        juce::PopupMenu sub;
        add (sub, "ch:" + juce::String (id) + ":vol", name + " volume");
        add (sub, "ch:" + juce::String (id) + ":pan", name + " pan");
        if (auto* p = ctx.engine->channelProcessor (id))
        {
            auto& ps = p->getParameters();
            for (int i = 0; i < juce::jmin (96, ps.size()); ++i)
                add (sub, "ch:" + juce::String (id) + ":p:" + juce::String (i), name + " " + ps[i]->getName (24));
        }
        chans.addSubMenu (name, sub);
    }
    menu.addSubMenu ("Channels", chans);
    juce::PopupMenu mix;
    for (int t = 0; t < numMixerTracks; ++t)
    {
        auto mt = ctx.project.mixerTrack (t);
        const auto name = mt.getProperty (ids::name).toString();
        juce::PopupMenu sub;
        add (sub, "mx:" + juce::String (t) + ":vol", name + " volume");
        add (sub, "mx:" + juce::String (t) + ":pan", name + " pan");
        for (auto slot : mt)
        {
            int si = (int) slot.getProperty (ids::index);
            if (auto* p = ctx.engine->slotProcessor (t, si))
            {
                juce::PopupMenu ps;
                auto& params = p->getParameters();
                for (int i = 0; i < juce::jmin (96, params.size()); ++i)
                    add (ps, "mx:" + juce::String (t) + ":" + juce::String (si) + ":p:" + juce::String (i), p->getName() + " " + params[i]->getName (24));
                sub.addSubMenu ("Slot " + juce::String (si + 1) + ": " + p->getName(), ps);
            }
        }
        mix.addSubMenu (juce::String (t) + " " + name, sub);
    }
    menu.addSubMenu ("Mixer", mix);
}
}
