// metar_cpp -> struct ws_metar_obs (see ws/metar_map.h).
//
// The only C++ in fw/lib. Built when the decoder is available: in Zephyr with
// CONFIG_WS_METAR, on the host always (fw/tests/host).
#include <cstring>

#include <Clouds.h>
#include <Metar.h>
#include <Phenom.h>
#include <RunwayState.h>

#include <ws/metar_map.h>

using Storage_B::Weather::Clouds;
using Storage_B::Weather::Metar;
using Storage_B::Weather::Phenom;
using Storage_B::Weather::RunwayState;

namespace
{

uint16_t wx_bit(Phenom::phenom p)
{
	switch (p) {
	case Phenom::phenom::RAIN:
		return WS_WX_RA;
	case Phenom::phenom::DRIZZLE:
		return WS_WX_DZ;
	case Phenom::phenom::SNOW:
		return WS_WX_SN;
	case Phenom::phenom::SNOW_GRAINS:
		return WS_WX_SG;
	case Phenom::phenom::ICE_PELLETS:
		return WS_WX_PL;
	case Phenom::phenom::HAIL:
		return WS_WX_GR;
	case Phenom::phenom::SMALL_HAIL:
		return WS_WX_GS;
	case Phenom::phenom::ICE_CRYSTALS:
		return WS_WX_IC;
	case Phenom::phenom::UNKNOWN_PRECIP:
		return WS_WX_UP;
	case Phenom::phenom::FOG:
		return WS_WX_FG;
	case Phenom::phenom::MIST:
		return WS_WX_BR;
	case Phenom::phenom::HAZE:
	case Phenom::phenom::SMOKE:
	case Phenom::phenom::DUST:
	case Phenom::phenom::SAND:
	case Phenom::phenom::VOLCANIC_ASH:
	case Phenom::phenom::SPRAY:
		return WS_WX_HZ;
	case Phenom::phenom::SQUALLS:
	case Phenom::phenom::FUNNEL_CLOUD:
		return WS_WX_SQ;
	case Phenom::phenom::DUST_STORM:
	case Phenom::phenom::SAND_STORM:
	case Phenom::phenom::DUST_SAND_WHORLS:
		return WS_WX_SS;
	default:
		return 0; // SHOWER is a descriptor here, NONE
	}
}

bool fill_ph(const Phenom &p, ws_metar_ph *out)
{
	std::memset(out, 0, sizeof(*out));
	for (unsigned i = 0; i < p.NumPhenom(); i++) {
		auto code = p[i];

		if (code == Phenom::phenom::SHOWER) {
			out->shower = true;
		}
		out->codes |= wx_bit(code);
	}
	out->intensity = static_cast<int8_t>(p.Intensity());
	out->freezing = p.Freezing();
	out->thunder = p.ThunderStorm();
	out->vicinity = p.Vicinity();
	// a bare SH or TS still counts: showers without type are rain
	if (out->shower && !(out->codes & 0x1ff)) { // WS_WX_RA..WS_WX_UP
		out->codes |= WS_WX_RA;
	}
	return out->codes || out->thunder;
}

uint8_t cover_of(Clouds::cover c)
{
	switch (c) {
	case Clouds::cover::FEW:
		return WS_CLD_FEW;
	case Clouds::cover::SCT:
		return WS_CLD_SCT;
	case Clouds::cover::BKN:
		return WS_CLD_BKN;
	case Clouds::cover::OVC:
		return WS_CLD_OVC;
	default:
		return WS_CLD_NONE;
	}
}

} // namespace

extern "C" int ws_metar_decode(const char *raw, struct ws_metar_obs *o)
{
	std::memset(o, 0, sizeof(*o));
	o->day = o->hour = o->minute = -1;
	if (!raw || std::strlen(raw) >= WS_METAR_MAX_LEN) {
		return -1;
	}
	auto m = Metar::Create(raw);

	if (!m) {
		return -1;
	}
	auto icao = m->ICAO();

	if (!icao || icao->size() != 4 || !m->Day()) {
		return -1;
	}
	std::memcpy(o->icao, icao->c_str(), 5);
	o->day = static_cast<int8_t>(*m->Day());
	o->hour = static_cast<int8_t>(m->Hour().value_or(-1));
	o->minute = static_cast<int8_t>(m->Minute().value_or(-1));

	if (auto t = m->Temperature()) {
		o->has_t = true;
		o->t = static_cast<int16_t>(*t);
	}
	if (auto d = m->DewPoint()) {
		o->has_dew = true;
		o->dew = static_cast<int16_t>(*d);
	}
	if (auto s = m->WindSpeed()) {
		o->has_wind = true;
		o->wind_speed = static_cast<int16_t>(*s);
		o->wind_dir = m->isVariableWindDirection() ? -1
							   : static_cast<int16_t>(m->WindDirection().value_or(-1));
		switch (m->WindSpeedUnits().value_or(Metar::speed_units::KT)) {
		case Metar::speed_units::MPS:
			o->wind_units = WS_WIND_MPS;
			break;
		case Metar::speed_units::KPH:
			o->wind_units = WS_WIND_KMH;
			break;
		default:
			o->wind_units = WS_WIND_KT;
			break;
		}
	}
	if (auto q = m->AltimeterQ()) {
		o->has_qnh = true;
		o->qnh_hpa = static_cast<int16_t>(*q);
	}
	if (auto a = m->AltimeterA()) {
		o->has_alt_a = true;
		o->alt_a = static_cast<int16_t>(*a * 100.0 + 0.5);
	}
	if (auto q = m->QFEmmHg()) {
		o->has_qfe_mm = true;
		o->qfe_mm = static_cast<int16_t>(*q);
	}
	if (auto q = m->QFEhPa()) {
		o->has_qfe_hpa = true;
		o->qfe_hpa = static_cast<int16_t>(*q);
	}
	o->cavok = m->isCAVOK();
	for (unsigned i = 0; i < m->NumCloudLayers(); i++) {
		auto l = m->Layer(i);

		if (l && !l->Temporary()) {
			uint8_t c = cover_of(l->Cover());

			if (c > o->cover) {
				o->cover = c;
			}
		}
	}
	if (m->VerticalVisibility()) {
		o->cover = WS_CLD_VV;
	}
	for (unsigned i = 0; i < m->NumPhenomena() && o->n_ph < WS_METAR_MAX_PH; i++) {
		const Phenom &p = m->Phenomenon(i);

		if (!p.Temporary() && fill_ph(p, &o->ph[o->n_ph])) {
			o->n_ph++;
		}
	}
	for (unsigned i = 0; i < m->NumRecentPhenomena() && o->n_re < WS_METAR_MAX_PH; i++) {
		if (fill_ph(m->RecentPhenomenon(i), &o->re[o->n_re])) {
			o->n_re++;
		}
	}
	for (unsigned i = 0; i < m->NumRunwayStates() && o->n_rwy < WS_METAR_MAX_RWY; i++) {
		const RunwayState *r = m->RunwayStateAt(i);

		if (r && !r->cleared && r->deposit_type) {
			o->rwy_deposit[o->n_rwy++] = static_cast<uint8_t>(*r->deposit_type);
		}
	}
	return 0;
}
