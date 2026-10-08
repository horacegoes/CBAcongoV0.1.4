#pragma once
// =============================================================================
// S950TestTone.h  -  Akai S950 factory test tone (C3, 261.68 Hz) as a seamless loop
//
// Source: Akai_S950_Test_Tone-C3.aif (16-bit mono, 44.1 kHz). Clean sine, harmonics
// more than 50 dB down. One cycle is 168.53 samples, so the loop is the smallest
// whole-sample seamless section: 2 cycles = 337 samples, starting on a rising zero
// crossing at sample 40440. Looped pitch 261.72 Hz (+0.27 cents); looping adds no
// spurs (worst unwanted component -50.7 dB, same as the original). Loop level:
// peak -2.7 dBFS (0.735).
//
// Playback follows the host rate via cubic interpolation, so it stays at C3 at any
// sample rate, with a 5 ms fade in/out so pressing TEST never clicks.
// Header-only C++17, no JUCE dependency, no allocation, real-time safe.
// =============================================================================

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cba
{
namespace s950tone
{
    constexpr double sourceRate = 44100.0;
    constexpr int    loopLength = 337;
    static const int16_t loop[loopLength] =
    {
           143,   1038,   1940,   2841,   3750,   4619,   5505,   6347,   7197,   8056,   8891,   9713,
         10517,  11314,  12098,  12858,  13597,  14307,  15009,  15700,  16312,  16964,  17579,  18156,
         18718,  19243,  19764,  20232,  20665,  21119,  21492,  21860,  22197,  22495,  22750,  22972,
         23185,  23345,  23493,  23594,  23669,  23719,  23718,  23716,  23629,  23539,  23414,  23260,
         23077,  22863,  22608,  22335,  22017,  21663,  21307,  20906,  20465,  20010,  19521,  18984,
         18445,  17895,  17311,  16675,  16053,  15374,  14716,  14000,  13281,  12551,  11780,  11005,
         10219,   9434,   8574,   7739,   6915,   6048,   5169,   4318,   3461,   2584,   1687,    782,
          -102,  -1007,  -1903,  -2783,  -3641,  -4522,  -5400,  -6259,  -7118,  -7961,  -8793,  -9625,
        -10420, -11238, -12007, -12808, -13544, -14278, -15012, -15669, -16345, -16989, -17605, -18201,
        -18771, -19336, -19845, -20344, -20808, -21253, -21654, -22044, -22376, -22684, -22979, -23219,
        -23444, -23631, -23787, -23903, -24011, -24084, -24090, -24097, -24038, -23966, -23859, -23712,
        -23546, -23332, -23096, -22825, -22519, -22191, -21833, -21441, -20999, -20546, -20096, -19578,
        -19030, -18475, -17895, -17286, -16631, -15981, -15301, -14592, -13846, -13105, -12342, -11556,
        -10754,  -9948,  -9147,  -8289,  -7404,  -6565,  -5703,  -4816,  -3954,  -3038,  -2167,  -1262,
          -367,    516,   1408,   2334,   3235,   4092,   4980,   5868,   6719,   7571,   8404,   9238,
         10056,  10850,  11639,  12440,  13167,  13887,  14616,  15282,  15967,  16609,  17234,  17851,
         18398,  18946,  19462,  19937,  20410,  20850,  21244,  21645,  22001,  22326,  22609,  22864,
         23072,  23266,  23399,  23542,  23618,  23681,  23708,  23682,  23657,  23568,  23485,  23327,
         23164,  22968,  22734,  22483,  22189,  21849,  21496,  21085,  20671,  20247,  19773,  19276,
         18739,  18189,  17622,  17019,  16393,  15735,  15077,  14384,  13673,  12935,  12199,  11414,
         10638,   9851,   9015,   8176,   7358,   6510,   5654,   4793,   3935,   3062,   2155,   1274,
           377,   -490,  -1402,  -2296,  -3171,  -4038,  -4919,  -5780,  -6652,  -7502,  -8350,  -9156,
         -9984, -10802, -11577, -12360, -13121, -13870, -14557, -15280, -15953, -16621, -17248, -17844,
        -18440, -18991, -19539, -20029, -20523, -20960, -21388, -21778, -22134, -22486, -22789, -23047,
        -23285, -23488, -23660, -23804, -23892, -23981, -24018, -24035, -24002, -23962, -23871, -23753,
        -23580, -23397, -23166, -22907, -22620, -22311, -21963, -21584, -21187, -20729, -20255, -19782,
        -19256, -18686, -18109, -17536, -16891, -16235, -15560, -14881, -14177, -13429, -12658, -11896,
        -11117, -10294,  -9471,  -8645,  -7789,  -6935,  -6069,  -5209,  -4328,  -3447,  -2539,  -1643,
          -749,
    };
}

class S950TestTone
{
public:
    void prepare (double sampleRate) noexcept
    {
        step = s950tone::sourceRate / sampleRate;
        fadeStep = 1.0f / (float) std::max (1.0, 0.005 * sampleRate);
        pos = 0.0;  fade = 0.0f;  on = false;
    }

    void setActive (bool shouldPlay) noexcept { on = shouldPlay; }
    bool isSounding() const noexcept { return on || fade > 0.0f; }
    float currentFade() const noexcept { return fade; }

    // one sample of the tone, with the fade applied; gain 1.0 = original recorded level
    float next (float gain = 1.0f) noexcept
    {
        if (on) fade = std::min (1.0f, fade + fadeStep);
        else    fade = std::max (0.0f, fade - fadeStep);
        if (fade <= 0.0f) { pos = 0.0; return 0.0f; }          // restarts on its zero crossing

        const int   i = (int) pos;
        const float t = (float) (pos - i);
        const float y0 = at (i - 1), y1 = at (i), y2 = at (i + 1), y3 = at (i + 2);
        const float c1 = 0.5f * (y2 - y0);                     // Catmull-Rom cubic
        const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
        const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        const float v  = ((c3 * t + c2) * t + c1) * t + y1;

        pos += step;
        if (pos >= s950tone::loopLength) pos -= s950tone::loopLength;
        return v * fade * gain;
    }

    // While TEST is on, crossfade the block's input to the tone so it runs through
    // the whole effect chain (sampler / stretch / filter). Mono tone on every channel.
    void replaceInput (float* const* io, int numChannels, int numSamples, float gain = 1.0f) noexcept
    {
        if (! isSounding()) return;
        for (int n = 0; n < numSamples; ++n)
        {
            const float v = next (gain);
            const float f = fade;
            for (int c = 0; c < numChannels; ++c)
                io[c][n] = io[c][n] * (1.0f - f) + v;
        }
    }

private:
    static float at (int k) noexcept
    {
        k %= s950tone::loopLength;
        if (k < 0) k += s950tone::loopLength;
        return (float) s950tone::loop[k] / 32768.0f;
    }

    double step = 1.0, pos = 0.0;
    float  fade = 0.0f, fadeStep = 0.005f;
    bool   on = false;
};

} // namespace cba

/* =============================================================================
   JUCE integration  -  a TEST button on the front panel

   // 1) Parameter (in createParameterLayout):
   layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "test", 1 },
                                                         "Test", false));

   // 2) Processor members / prepareToPlay:
   cba::S950TestTone testTone;
   std::atomic<float>* testParam = nullptr;   // = apvts.getRawParameterValue ("test") in the constructor
   testTone.prepare (sampleRate);

   // 3) processBlock - FIRST, so the tone feeds the sampler / stretch / filter chain:
   testTone.setActive (testParam->load() > 0.5f);
   testTone.replaceInput (buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
                          buffer.getNumSamples());
   //    ... then the rest of the chain as normal

   // 4) Editor members:
   juce::TextButton testButton { "TEST" };
   std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> testAttach;

   //    Editor constructor:
   testButton.setClickingTogglesState (true);   // press = on, press again = off
   addAndMakeVisible (testButton);
   testAttach = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
                    processor.apvts, "test", testButton);

   //    resized():
   testButton.setBounds (10, 10, 70, 28);

   //    Hold-to-play instead: skip setClickingTogglesState and in testButton.onStateChange
   //    set the "test" parameter to testButton.isDown().
   ========================================================================== */
