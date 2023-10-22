#include "PluginProcessor.h"
#include "dsp/Distortion.h"
#include "../arch/Math.h"

namespace audio
{
	PluginProcessor::PluginProcessor(Params& _params) :
		params(_params),
		sampleRate(1.)
	{
	}

	void PluginProcessor::prepare(double _sampleRate)
	{
		sampleRate = _sampleRate;
	}

	void PluginProcessor::operator()(double** samples, dsp::MidiBuffer&, int numChannels, int numSamples) noexcept
	{
		const auto slewPitch = params(PID::Slew).getValueDenorm();
		const auto slewHz = math::noteInFreqHz2(slewPitch);
		const auto slewRate = slew.freqHzToSlewRate(slewHz, sampleRate);
		const auto type = int(std::round(params(PID::FilterType).getValueDenorm()));
		slew(samples, slewRate, numChannels, numSamples, dsp::SlewLimiter::Type(type));
	}

	void PluginProcessor::processBlockBypassed(double**, dsp::MidiBuffer&, int, int) noexcept
	{}

	void PluginProcessor::savePatch()
	{}

	void PluginProcessor::loadPatch()
	{}
}