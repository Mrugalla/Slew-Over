#include "Param.h"
#include "../arch/FormulaParser.h"
#include "../arch/Math.h"
#include "../arch/Range.h"

namespace param
{
	String toID(const String& name)
	{
		return name.removeCharacters(" ").toLowerCase();
	}

	PID ll(PID pID, int off) noexcept
	{
		auto i = static_cast<int>(pID);
		i += (NumParams - 1) * off;
		return static_cast<PID>(i);
	}

	PID offset(PID pID, int off) noexcept
	{
		return static_cast<PID>(static_cast<int>(pID) + off);
	}

	String toString(PID pID)
	{
		switch (pID)
		{
		case PID::GainOut: return "Gain Out";
		case PID::HQ: return "HQ";
		case PID::Slew: return "Slew";
		case PID::FilterType: return "Filter Type";
		default: return "Invalid Parameter Name";
		}
	}

	PID toPID(const String& id)
	{
		const auto nID = toID(id);
		for (auto i = 0; i < NumParams; ++i)
		{
			auto pID = static_cast<PID>(i);
			if (nID == toID(toString(pID)))
				return pID;
		}
		return PID::NumParams;
	}

	void toPIDs(std::vector<PID>& pIDs, const String& text, const String& seperator)
	{
		auto tokens = juce::StringArray::fromTokens(text, seperator, "");
		for (auto& token : tokens)
		{
			auto pID = toPID(token);
			if (pID != PID::NumParams)
				pIDs.push_back(pID);
		}
	}

	String toTooltip(PID pID)
	{
		switch (pID)
		{
		case PID::GainOut: return "Apply gain to the output signal.";
		case PID::HQ: return "Apply oversampling to the signal.";
		case PID::FilterType: return "Choose the filter type. (LP or HP)";
		case PID::Slew: return "Apply the slew rate to the signal.";
		default: return "Invalid Tooltip.";
		}
	}

	String toString(Unit pID)
	{
		switch (pID)
		{
		case Unit::Power: return "";
		case Unit::Solo: return "S";
		case Unit::Mute: return "M";
		case Unit::Percent: return "%";
		case Unit::Hz: return "hz";
		case Unit::Beats: return "";
		case Unit::Degree: return CharPtr("\xc2\xb0");
		case Unit::Octaves: return "oct";
		case Unit::Semi: return "semi";
		case Unit::Fine: return "fine";
		case Unit::Ms: return "ms";
		case Unit::Decibel: return "db";
		case Unit::Ratio: return "ratio";
		case Unit::Polarity: return CharPtr("\xc2\xb0");
		case Unit::StereoConfig: return "";
		case Unit::Voices: return "v";
		case Unit::Pan: return "%";
		case Unit::Xen: return "notes/oct";
		case Unit::Note: return "";
		case Unit::Pitch: return "";
		case Unit::Q: return "q";
		case Unit::Slope: return "db/oct";
		case Unit::Legato: return "";
		case Unit::Custom: return "";
		case Unit::FilterType: return "";
		default: return "";
		}
	}

	// PARAM:

	Param::Param(const PID pID, const Range& _range, const float _valDenormDefault,
		const ValToStrFunc& _valToStr, const StrToValFunc& _strToVal, const Unit _unit) :
		AudioProcessorParameter(),
		id(pID),
		range(_range),
		valDenormDefault(range.snapToLegalValue(_valDenormDefault)),
		valNorm(range.convertTo0to1(valDenormDefault)),
		maxModDepth(0.f),
		valMod(valNorm.load()),
		modBias(.5f),
		valToStr(_valToStr),
		strToVal(_strToVal),
		unit(_unit),
		locked(false),
		inGesture(false),
		modDepthLocked(false)
	{
	}

	Param::Type Param::getType() const noexcept
	{
		const auto interval = range.interval;
		const bool stepped = interval == 1.f;

		if (!stepped)
			return Type::Float;

		if(range.start == 0.f && range.end == 1.f)
			return Type::Bool;
		
		return Type::Int;
	}

	void Param::savePatch(State& state) const
	{
		const String idStr("params/" + toID(toString(id)));

		const auto v = range.convertFrom0to1(getValue());
		state.set(idStr + "/value", v);
		const auto mdd = getMaxModDepth();
		state.set(idStr + "/maxmoddepth", mdd);
		const auto mb = getModBias();
		state.set(idStr + "/modbias", mb);
	}

	void Param::loadPatch(State& state)
	{
		const String idStr("params/" + toID(toString(id)));

		const auto lckd = isLocked();
		if (!lckd)
		{
			auto var = state.get(idStr + "/value");
			if (var)
			{
				const auto val = static_cast<float>(*var);
				const auto legalVal = range.snapToLegalValue(val);
				const auto valD = range.convertTo0to1(legalVal);
				setValueNotifyingHost(valD);
			}
			var = state.get(idStr + "/maxmoddepth");
			if (var)
			{
				const auto val = static_cast<float>(*var);
				setMaxModDepth(val);
			}
			var = state.get(idStr + "/modbias");
			if (var)
			{
				const auto val = static_cast<float>(*var);
				setModBias(val);
			}
		}
	}

	//called by host, normalized, thread-safe
	float Param::getValue() const
	{
		return valNorm.load();
	}

	float Param::getValueDenorm() const noexcept
	{
		return range.convertFrom0to1(getValue());
	}

	// called by host, normalized, avoid locks, not used (directly) by editor
	void Param::setValue(float normalized)
	{
		if (isLocked())
			return;

		if (!modDepthLocked)
			return valNorm.store(normalized);

		const auto p0 = valNorm.load();
		const auto p1 = normalized;

		const auto d0 = getMaxModDepth();
		const auto d1 = d0 - p1 + p0;

		valNorm.store(p1);
		setMaxModDepth(d1);
	}

	// called by editor
	bool Param::isInGesture() const noexcept
	{
		return inGesture.load();
	}

	void Param::setValueWithGesture(float norm)
	{
		if (isInGesture())
			return;
		beginChangeGesture();
		setValueNotifyingHost(norm);
		endChangeGesture();
	}

	void Param::beginGesture()
	{
		inGesture.store(true);
		beginChangeGesture();
	}

	void Param::endGesture()
	{
		inGesture.store(false);
		endChangeGesture();
	}

	float Param::getMaxModDepth() const noexcept
	{
		return maxModDepth.load();
	};

	void Param::setMaxModDepth(float v) noexcept
	{
		if (isLocked())
			return;

		maxModDepth.store(juce::jlimit(-1.f, 1.f, v));
	}

	float Param::calcValModOf(float macro) const noexcept
	{
		const auto norm = getValue();

		const auto mmd = maxModDepth.load();
		const auto pol = mmd > 0.f ? 1.f : -1.f;
		const auto md = mmd * pol;
		const auto mdSkew = biased(0.f, md, modBias.load(), macro);
		const auto mod = mdSkew * pol;

		return juce::jlimit(0.f, 1.f, norm + mod);
	}

	float Param::getValMod() const noexcept
	{
		return valMod.load();
	}

	float Param::getValModDenorm() const noexcept
	{
		return range.convertFrom0to1(valMod.load());
	}

	void Param::setModBias(float b) noexcept
	{
		if (isLocked())
			return;

		b = juce::jlimit(BiasEps, 1.f - BiasEps, b);
		modBias.store(b);
	}

	float Param::getModBias() const noexcept
	{
		return modBias.load();
	}

	void Param::setModDepthLocked(bool e) noexcept
	{
		modDepthLocked = e;
	}

	void Param::setDefaultValue(float norm) noexcept
	{
		valDenormDefault = range.convertFrom0to1(norm);
	}

	// called by processor to update modulation value(s)
	void Param::modulate(float macro) noexcept
	{
		valMod.store(calcValModOf(macro));
	}

	float Param::getDefaultValue() const
	{
		return range.convertTo0to1(valDenormDefault);
	}

	String Param::getName(int) const
	{
		return toString(id);
	}

	// units of param (hz, % etc.)
	String Param::getLabel() const
	{
		return toString(unit);
	}

	// string of norm val
	String Param::getText(float norm, int) const
	{
		return valToStr(range.snapToLegalValue(range.convertFrom0to1(norm)));
	}

	// string to norm val
	float Param::getValueForText(const String& text) const
	{
		const auto val = juce::jlimit(range.start, range.end, strToVal(text));
		return range.convertTo0to1(val);
	}

	// string to denorm val
	float Param::getValForTextDenorm(const String& text) const
	{
		return strToVal(text);
	}

	String Param::_toString()
	{
		auto v = getValue();
		return getName(10) + ": " + String(v) + "; " + getText(v, 10);
	}

	int Param::getNumSteps() const
	{
		if (range.interval > 0.f)
		{
			const auto numSteps = (range.end - range.start) / range.interval;
			return 1 + static_cast<int>(numSteps);
		}

		return juce::AudioProcessor::getDefaultNumParameterSteps();
	}

	bool Param::isLocked() const noexcept
	{
		return locked.load();
	}

	void Param::setLocked(bool e) noexcept
	{
		locked.store(e);
	}

	void Param::switchLock() noexcept
	{
		setLocked(!isLocked());
	}

	float Param::biased(float start, float end, float bias/*[0,1]*/, float x) const noexcept
	{
		const auto r = end - start;
		if (r == 0.f)
			return 0.f;
		const auto a2 = 2.f * bias;
		const auto aM = 1.f - bias;
		const auto aR = r * bias;
		return start + aR * x / (aM - x + a2 * x);
	}

}

namespace param::strToVal
{
	extern std::function<float(String, const float/*altVal*/)> parse()
	{
		return [](const String& txt, const float altVal)
		{
			fx::Parser fx;
			if (fx(txt))
				return fx();

			return altVal;
		};
	}

	StrToValFunc power()
	{
		return[p = parse()](const String& txt)
		{
			const auto text = txt.trimCharactersAtEnd(toString(Unit::Power));
			if (math::stringNegates(text))
				return 0.f;
			const auto val = p(text, 0.f);
			return val > .5f ? 1.f : 0.f;
		};
	}

	StrToValFunc solo()
	{
		return[p = parse()](const String& txt)
		{
			const auto text = txt.trimCharactersAtEnd(toString(Unit::Solo));
			const auto val = p(text, 0.f);
			return val > .5f ? 1.f : 0.f;
		};
	}

	StrToValFunc mute()
	{
		return[p = parse()](const String& txt)
		{
			const auto text = txt.trimCharactersAtEnd(toString(Unit::Mute));
			const auto val = p(text, 0.f);
			return val > .5f ? 1.f : 0.f;
		};
	}

	StrToValFunc percent()
	{
		return[p = parse()](const String& txt)
		{
			const auto text = txt.trimCharactersAtEnd(toString(Unit::Percent));
			const auto val = p(text, 0.f);
			return val * .01f;
		};
	}

	StrToValFunc hz()
	{
		return[p = parse()](const String& txt)
		{
			auto text = txt.trimCharactersAtEnd(toString(Unit::Hz));
			auto multiplier = 1.f;
			if (text.getLastCharacter() == 'k')
			{
				multiplier = 1000.f;
				text = text.dropLastCharacters(1);
			}
			const auto val = p(text, 0.f);
			const auto val2 = val * multiplier;

			return val2;
		};
	}

	StrToValFunc phase()
	{
		return[p = parse()](const String& txt)
		{
			const auto text = txt.trimCharactersAtEnd(toString(Unit::Degree));
			const auto val = p(text, 0.f);
			return val;
		};
	}

	StrToValFunc oct()
	{
		return[p = parse()](const String& txt)
		{
			const auto text = txt.trimCharactersAtEnd(toString(Unit::Octaves));
			const auto val = p(text, 0.f);
			return std::round(val);
		};
	}

	StrToValFunc semi()
	{
		return[p = parse()](const String& txt)
		{
			const auto text = txt.trimCharactersAtEnd(toString(Unit::Semi));
			const auto val = p(text, 0.f);
			return std::round(val);
		};
	}

	StrToValFunc fine()
	{
		return[p = parse()](const String& txt)
		{
			const auto text = txt.trimCharactersAtEnd(toString(Unit::Fine));
			const auto val = p(text, 0.f);
			return val * .01f;
		};
	}

	StrToValFunc ratio()
	{
		return[p = parse()](const String& txt)
		{
			const auto text = txt.trimCharactersAtEnd(toString(Unit::Ratio));
			const auto val = p(text, 0.f);
			return val * .01f;
		};
	}

	StrToValFunc lrms()
	{
		return [](const String& txt)
		{
			return txt[0] == 'l' ? 0.f : 1.f;
		};
	}

	StrToValFunc freeSync()
	{
		return [](const String& txt)
		{
			return txt[0] == 'f' ? 0.f : 1.f;
		};
	}

	StrToValFunc polarity()
	{
		return [](const String& txt)
		{
			return txt[0] == '0' ? 0.f : 1.f;
		};
	}

	StrToValFunc ms()
	{
		return[p = parse()](const String& txt)
		{
			const auto text = txt.trimCharactersAtEnd(toString(Unit::Ms));
			const auto val = p(text, 0.f);
			return val;
		};
	}

	StrToValFunc db()
	{
		return[p = parse()](const String& txt)
		{
			const auto text = txt.trimCharactersAtEnd(toString(Unit::Decibel));
			const auto val = p(text, 0.f);
			return val;
		};
	}

	StrToValFunc voices()
	{
		return[p = parse()](const String& txt)
		{
			const auto text = txt.trimCharactersAtEnd(toString(Unit::Voices));
			const auto val = p(text, 1.f);
			return val;
		};
	}

	StrToValFunc pan(const Params& params)
	{
		return[p = parse(), &prms = params](const String& txt)
		{
			if (txt == "center" || txt == "centre")
				return 0.f;

			const auto text = txt.trimCharactersAtEnd("MSLR").toLowerCase();
#if PPDHasStereoConfig
			const auto sc = prms[PID::StereoConfig];
			if (sc->getValMod() < .5f)
#endif
			{
				if (txt == "l" || txt == "left")
					return -1.f;
				else if (txt == "r" || txt == "right")
					return 1.f;
			}
#if PPDHasStereoConfig
			else
			{

				if (txt == "m" || txt == "mid")
					return -1.f;
				else if (txt == "s" || txt == "side")
					return 1.f;
			}
#endif

			const auto val = p(text, 0.f);
			return val * .01f;
		};
	}

	StrToValFunc xen()
	{
		return[p = parse()](const String& txt)
		{
			const auto text = txt.trimCharactersAtEnd(toString(Unit::Xen));
			const auto val = p(text, 0.f);
			return val;
		};
	}

	StrToValFunc note()
	{
		return[p = parse()](const String& txt)
		{
			const auto text = txt.toLowerCase();
			auto val = p(text, -1.f);
			if (val >= 0.f && val < 128.f)
				return val;

			enum pitchclass { C, Db, D, Eb, E, F, Gb, G, Ab, A, Bb, B, Num };
			enum class State { Pitchclass, FlatOrSharp, Parse, numStates };

			auto state = State::Pitchclass;

			for (auto i = 0; i < text.length(); ++i)
			{
				auto chr = text[i];

				if (state == State::Pitchclass)
				{
					if (chr == 'c')
						val = C;
					else if (chr == 'd')
						val = D;
					else if (chr == 'e')
						val = E;
					else if (chr == 'f')
						val = F;
					else if (chr == 'g')
						val = G;
					else if (chr == 'a')
						val = A;
					else if (chr == 'b')
						val = B;
					else
						return 69.f;

					state = State::FlatOrSharp;
				}
				else if (state == State::FlatOrSharp)
				{
					if (chr == '#')
						++val;
					else if (chr == 'b')
						--val;
					else
						--i;

					state = State::Parse;
				}
				else if (state == State::Parse)
				{
					auto newVal = p(text.substring(i), -1.f);
					if (newVal == -1.f)
						return 69.f;
					val += 12 + newVal * 12.f;
					while (val < 0.f)
						val += 12.f;
					return val;
				}
				else
					return 69.f;
			}

			return juce::jlimit(0.f, 127.f, val + 12.f);
		};
	}

	StrToValFunc pitch(const Xen& xenManager)
	{
		return[hzFunc = hz(), noteFunc = note(), &xen = xenManager](const String& txt)
		{
			auto freqHz = hzFunc(txt);
			if (freqHz != 0.f)
				return xen.freqHzToNote(freqHz);

			return noteFunc(txt);
		};
	}

	StrToValFunc pitch()
	{
		return[hzFunc = hz(), noteFunc = note()](const String& txt)
			{
				auto freqHz = hzFunc(txt);
				if (freqHz != 0.f)
					return math::freqHzInNote2(freqHz);

				return noteFunc(txt);
			};
	}

	StrToValFunc q()
	{
		return[p = parse()](const String& txt)
		{
			const auto text = txt.trimCharactersAtEnd(toString(Unit::Q));
			const auto val = p(text, 40.f);
			return val;
		};
	}

	StrToValFunc slope()
	{
		return[p = parse()](const String& txt)
		{
			const auto text = txt.trimCharactersAtEnd(toString(Unit::Slope));
			const auto val = p(text, 0.f);
			return val / 12.f;
		};
	}

	StrToValFunc beats()
	{
		return[p = parse()](const String& txt)
		{
			enum Mode { Beats, Triplet, Dotted, NumModes };
			const auto lastChr = txt[txt.length() - 1];
			const auto mode = lastChr == 't' ? Mode::Triplet : lastChr == '.' ? Mode::Dotted : Mode::Beats;

			const auto text = mode == Mode::Beats ? txt : txt.substring(0, txt.length() - 1);
			auto val = p(text, 1.f / 16.f);
			if (mode == Mode::Triplet)
				val *= 1.666666666667f;
			else if (mode == Mode::Dotted)
				val *= 1.75f;
			return val;
		};
	}

	StrToValFunc legato()
	{
		return[p = parse()](const String& txt)
		{
			if (math::stringNegates(txt))
				return 0.f;
			return p(txt, 0.f);
		};
	}

	StrToValFunc filterType()
	{
		return[p = parse()](const String& txt)
		{
			auto text = txt.toLowerCase();
			if (text == "lp")
				return 0.f;
			else if (text == "hp")
				return 1.f;
			else if (text == "bp")
				return 2.f;
			else if (text == "br")
				return 3.f;
			else if (text == "ap")
				return 4.f;
			else if (text == "ls")
				return 5.f;
			else if (text == "hs")
				return 6.f;
			else if (text == "notch")
				return 7.f;
			else if (text == "bell")
				return 8.f;
			else
				return p(text, 0.f);
		};
	}
}

namespace param::valToStr
{
	ValToStrFunc mute()
	{
		return [](float v) { return v > .5f ? "Mute" : "Not Mute"; };
	}

	ValToStrFunc solo()
	{
		return [](float v) { return v > .5f ? "Solo" : "Not Solo"; };
	}

	ValToStrFunc power()
	{
		return [](float v) { return v > .5f ? "Enabled" : "Disabled"; };
	}

	ValToStrFunc percent()
	{
		return [](float v) { return String(std::round(v * 100.f)) + " " + toString(Unit::Percent); };
	}

	ValToStrFunc hz()
	{
		return [](float v)
		{
			if (v >= 10000.f)
				return String(v * .001).substring(0, 4) + " k" + toString(Unit::Hz);
			else if (v >= 1000.f)
				return String(v * .001).substring(0, 3) + " k" + toString(Unit::Hz);
			else
				return String(v).substring(0, 5) + " " + toString(Unit::Hz);
		};
	}

	ValToStrFunc phase()
	{
		return [](float v) { return String(std::round(v * 180.f)) + " " + toString(Unit::Degree); };
	}

	ValToStrFunc phase360()
	{
		return [](float v) { return String(std::round(v * 360.f)) + " " + toString(Unit::Degree); };
	}

	ValToStrFunc oct()
	{
		return [](float v) { return String(std::round(v)) + " " + toString(Unit::Octaves); };
	}

	ValToStrFunc semi()
	{
		return [](float v) { return String(std::round(v)) + " " + toString(Unit::Semi); };
	}

	ValToStrFunc fine()
	{
		return [](float v) { return String(std::round(v * 100.f)) + " " + toString(Unit::Fine); };
	}

	ValToStrFunc ratio()
	{
		return [](float v)
		{
			const auto y = static_cast<int>(std::round(v * 100.f));
			return String(100 - y) + " : " + String(y);
		};
	}

	ValToStrFunc lrms()
	{
		return [](float v) { return v > .5f ? String("m/s") : String("l/r"); };
	}

	ValToStrFunc freeSync()
	{
		return [](float v) { return v > .5f ? String("sync") : String("free"); };
	}

	ValToStrFunc polarity()
	{
		return [](float v) { return v > .5f ? String("on") : String("off"); };
	}

	ValToStrFunc ms()
	{
		return [](float v) { return String(std::round(v * 10.f) * .1f) + " " + toString(Unit::Ms); };
	}

	ValToStrFunc db()
	{
		return [](float v) { return String(std::round(v * 100.f) * .01f) + " " + toString(Unit::Decibel); };
	}

	ValToStrFunc empty()
	{
		return [](float) { return String(""); };
	}

	ValToStrFunc voices()
	{
		return [](float v)
		{
			return String(std::round(v)) + toString(Unit::Voices);
		};
	}

	ValToStrFunc pan(const Params& params)
	{
		return [&prms = params](float v)
		{
			if (v == 0.f)
				return String("C");

#if PPDHasStereoConfig
			const auto sc = prms[PID::StereoConfig];
			const auto vm = sc->getValMod();
			const auto isMidSide = vm > .5f;

			if (!isMidSide)
#endif
			{
				if (v == -1.f)
					return String("Left");
				else if (v == 1.f)
					return String("Right");
				else
					return String(std::round(v * 100.f)) + (v < 0.f ? " L" : " R");
			}
#if PPDHasStereoConfig
			else
			{
				if (v == -1.f)
					return String("Mid");
				else if (v == 1.f)
					return String("Side");
				else
					return String(std::round(v * 100.f)) + (v < 0.f ? " M" : " S");
			}
#endif
		};
	}

	ValToStrFunc xen()
	{
		return [](float v)
		{
			return String(std::round(v)) + " " + toString(Unit::Xen);
		};
	}

	ValToStrFunc note()
	{
		return [](float v)
		{
			if (v >= 0.f)
			{
				enum pitchclass { C, Db, D, Eb, E, F, Gb, G, Ab, A, Bb, B, Num };

				const auto note = static_cast<int>(std::round(v));
				const auto octave = note / 12 - 1;
				const auto noteName = note % 12;
				return math::pitchclassToString(noteName) + String(octave);
			}
			return String("?");
		};
	}

	ValToStrFunc pitch(const Xen& xenManager)
	{
		return [noteFunc = note(), hzFunc = hz(), &xen = xenManager](float v)
		{
			return noteFunc(v) + "; " + hzFunc(xen.noteToFreqHz(v));
		};
	}

	ValToStrFunc pitch()
	{
		return [noteFunc = note(), hzFunc = hz()](float v)
			{
				return noteFunc(v) + "; " + hzFunc(math::noteInFreqHz2(v));
			};
	}

	ValToStrFunc q()
	{
		return [](float v)
		{
			v = std::round(v * 100.f) * .01f;
			return String(v) + " " + toString(Unit::Q);
		};
	}

	ValToStrFunc slope()
	{
		return [](float v)
		{
			v = std::round(v) * 12.f;
			return String(v) + " " + toString(Unit::Slope);
		};
	}

	ValToStrFunc beats()
	{
		enum Mode { Whole, Triplet, Dotted, NumModes };

		return [](float v)
		{
			if (v == 0.f)
				return String("0");

			const auto denormFloor = math::nextLowestPowTwoX(v);
			const auto denormFrac = v - denormFloor;
			const auto modeVal = denormFrac / denormFloor;
			const auto mode = modeVal < .66f ? Mode::Whole :
				modeVal < .75f ? Mode::Triplet :
				Mode::Dotted;
			String modeStr = mode == Mode::Whole ? String("") :
				mode == Mode::Triplet ? String("t") :
				String(".");

			auto denominator = 1.f / denormFloor;
			auto numerator = 1.f;
			if (denominator < 1.f)
			{
				numerator = denormFloor;
				denominator = 1.f;
			}

			return String(numerator) + " / " + String(denominator) + modeStr;
		};
	}

	ValToStrFunc legato()
	{
		return [](float v)
		{
			return v < .5f ? String("Off") : v < 1.5f ? String("On") : String("On+Sus");
		};
	}

	ValToStrFunc filterType()
	{
		return [](float v)
		{
			auto idx = static_cast<int>(std::round(v));
			switch (idx)
			{
			case 0: return String("LP");
			case 1: return String("HP");
			case 2: return String("BP");
			case 3: return String("BR");
			case 4: return String("AP");
			case 5: return String("LS");
			case 6: return String("HS");
			case 7: return String("Notch");
			case 8: return String("Bell");
			default: return String("");
			}
		};
	}
}

namespace param
{
	/* pID, valDenormDefault, range, Unit */
	extern Param* makeParam(PID id, float valDenormDefault = 1.f,
		const Range& range = { 0.f, 1.f }, Unit unit = Unit::Percent)
	{
		ValToStrFunc valToStrFunc;
		StrToValFunc strToValFunc;

		switch (unit)
		{
		case Unit::Power:
			valToStrFunc = valToStr::power();
			strToValFunc = strToVal::power();
			break;
		case Unit::Solo:
			valToStrFunc = valToStr::solo();
			strToValFunc = strToVal::solo();
			break;
		case Unit::Mute:
			valToStrFunc = valToStr::mute();
			strToValFunc = strToVal::mute();
			break;
		case Unit::Decibel:
			valToStrFunc = valToStr::db();
			strToValFunc = strToVal::db();
			break;
		case Unit::Ms:
			valToStrFunc = valToStr::ms();
			strToValFunc = strToVal::ms();
			break;
		case Unit::Percent:
			valToStrFunc = valToStr::percent();
			strToValFunc = strToVal::percent();
			break;
		case Unit::Hz:
			valToStrFunc = valToStr::hz();
			strToValFunc = strToVal::hz();
			break;
		case Unit::Ratio:
			valToStrFunc = valToStr::ratio();
			strToValFunc = strToVal::ratio();
			break;
		case Unit::Polarity:
			valToStrFunc = valToStr::polarity();
			strToValFunc = strToVal::polarity();
			break;
		case Unit::StereoConfig:
			valToStrFunc = valToStr::lrms();
			strToValFunc = strToVal::lrms();
			break;
		case Unit::Octaves:
			valToStrFunc = valToStr::oct();
			strToValFunc = strToVal::oct();
			break;
		case Unit::Semi:
			valToStrFunc = valToStr::semi();
			strToValFunc = strToVal::semi();
			break;
		case Unit::Fine:
			valToStrFunc = valToStr::fine();
			strToValFunc = strToVal::fine();
			break;
		case Unit::Voices:
			valToStrFunc = valToStr::voices();
			strToValFunc = strToVal::voices();
			break;
		case Unit::Xen:
			valToStrFunc = valToStr::xen();
			strToValFunc = strToVal::xen();
			break;
		case Unit::Note:
			valToStrFunc = valToStr::note();
			strToValFunc = strToVal::note();
			break;
		case Unit::Q:
			valToStrFunc = valToStr::q();
			strToValFunc = strToVal::q();
			break;
		case Unit::Slope:
			valToStrFunc = valToStr::slope();
			strToValFunc = strToVal::slope();
			break;
		case Unit::Beats:
			valToStrFunc = valToStr::beats();
			strToValFunc = strToVal::beats();
			break;
		case Unit::Legato:
			valToStrFunc = valToStr::legato();
			strToValFunc = strToVal::legato();
			break;
		case Unit::FilterType:
			valToStrFunc = valToStr::filterType();
			strToValFunc = strToVal::filterType();
			break;
		case Unit::Pitch:
			valToStrFunc = valToStr::pitch();
			strToValFunc = strToVal::pitch();
			break;
		default:
			valToStrFunc = [](float v) { return String(v); };
			strToValFunc = [p = param::strToVal::parse()](const String& s)
			{
				return p(s, 0.f);
			};
			break;
		}

		return new Param(id, range, valDenormDefault, valToStrFunc, strToValFunc, unit);
	}

	/* pID, params */
	extern Param* makeParamPan(PID id, const Params& params)
	{
		ValToStrFunc valToStrFunc = valToStr::pan(params);
		StrToValFunc strToValFunc = strToVal::pan(params);

		return new Param(id, { -1.f, 1.f }, 0.f, valToStrFunc, strToValFunc, Unit::Pan);
	}

	/* pID, state, valDenormDefault, range, Xen */
	extern Param* makeParamPitch(PID id, float valDenormDefault,
		const Range& range, const Xen& xen)
	{
		ValToStrFunc valToStrFunc = valToStr::pitch(xen);
		StrToValFunc strToValFunc = strToVal::pitch(xen);

		return new Param(id, range, valDenormDefault, valToStrFunc, strToValFunc, Unit::Pitch);
	}

	extern Param* makeParam(PID id, float valDenormDefault, const Range& range,
		ValToStrFunc&& valToStrFunc, StrToValFunc&& strToValFunc)
	{
		return new Param(id, range, valDenormDefault, valToStrFunc, strToValFunc, Unit::Custom);
	}

	// PARAMS

	Params::Params(AudioProcessor& audioProcessor
#if PPDHasTuningEditor
		, const Xen& xen
#endif
	) :
		params(),
		modDepthLocked(false)
	{
		params.push_back(makeParam(PID::Slew, 36.f, makeRange::lin(0.f, 127.f), Unit::Pitch));
		params.push_back(makeParam(PID::FilterType, 0.f, makeRange::stepped(0.f, 1.f), Unit::FilterType));
		const auto gainOutRange = makeRange::withCentre(PPDGainOutMin, PPDGainOutMax, 0.f);
		params.push_back(makeParam(PID::GainOut, 0.f, gainOutRange, Unit::Decibel));
		params.push_back(makeParam(PID::HQ, 0.f, makeRange::toggle(), Unit::Power));
		
		for (auto param : params)
			audioProcessor.addParameter(param);
	}

	void Params::loadPatch(State& state)
	{
		const String idStr("params");
		const auto mdl = state.get(idStr + "/moddepthlocked");
		if (mdl != nullptr)
			setModDepthLocked(static_cast<int>(*mdl) != 0);

		for (auto param : params)
			param->loadPatch(state);
	}

	void Params::savePatch(State& state) const
	{
		for (auto param : params)
			param->savePatch(state);

		const String idStr("params");
		state.set(idStr + "/moddepthlocked", (isModDepthLocked() ? 1 : 0));
	}

	int Params::getParamIdx(const String& nameOrID) const
	{
		for (auto p = 0; p < params.size(); ++p)
		{
			const auto pName = toString(params[p]->id);
			if (nameOrID == pName || nameOrID == toID(pName))
				return p;
		}
		return -1;
	}

	size_t Params::numParams() const noexcept { return params.size(); }

	Param* Params::operator[](int i) noexcept { return params[i]; }
	const Param* Params::operator[](int i) const noexcept { return params[i]; }
	Param* Params::operator[](PID p) noexcept { return params[static_cast<int>(p)]; }
	const Param* Params::operator[](PID p) const noexcept { return params[static_cast<int>(p)]; }

	Param& Params::operator()(int i) noexcept { return *params[i]; }
	const Param& Params::operator()(int i) const noexcept { return *params[i]; }
	Param& Params::operator()(PID p) noexcept { return *params[static_cast<int>(p)]; }
	const Param& Params::operator()(PID p) const noexcept { return *params[static_cast<int>(p)]; }

	Params::Parameters& Params::data() noexcept { return params; }

	const Params::Parameters& Params::data() const noexcept { return params; }

	bool Params::isModDepthLocked() const noexcept { return modDepthLocked.load(); }

	void Params::setModDepthLocked(bool e) noexcept
	{
		modDepthLocked.store(e);
		for (auto& p : params)
			p->setModDepthLocked(e);
	}

	void Params::switchModDepthLocked() noexcept
	{
		setModDepthLocked(!isModDepthLocked());
	}

	// MACRO PROCESSOR

	void processMacroMod(Params&) noexcept
	{
		
	}
}